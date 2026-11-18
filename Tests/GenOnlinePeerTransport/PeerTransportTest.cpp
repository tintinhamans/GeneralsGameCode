// Tests the per-peer transport layer without GameNetworkingSockets: the slot that holds the transport of a connection,
// the route text and the plugin adapter over a fake plugin table.
// It also tests the provider boundary: the log sink, the signal queues and the network config mapping.

#include "GameNetwork/PeerTransport/PeerTransport.h"
#include "GameNetwork/PeerTransport/PluginPeerTransport.h"
#include "GameNetwork/PeerTransport/PeerSignalChannel.h"
#include "GameNetwork/PeerTransport/GnsNetworkConfig.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

static int g_failures = 0;

#define CHECK(condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			++g_failures; \
		} \
	} while (0)

// The calls a fake transport saw. They outlive the transport, so a test can read them after a replacement.
struct FakeCalls
{
	int closes = 0;
	std::string lastCloseReason;
	int sends = 0;
	int lastSendFlags = 0;
	std::vector<uint8_t> lastSend;
};

class FakeTransport : public IPeerTransport
{
public:
	FakeTransport(std::shared_ptr<FakeCalls> calls, const char* szLabel, PeerRoute route)
		: m_calls(std::move(calls))
		, m_label(szLabel)
		, m_route(route)
	{
	}
	virtual ~FakeTransport() override {}

	virtual PeerTransportKind GetKind() const override { return PeerTransportKind::Plugin; }
	virtual std::string GetLabel() const override { return m_label; }
	virtual bool CanSend() const override { return m_open; }
	virtual void Close(const char* szReason) override
	{
		++m_calls->closes;
		m_calls->lastCloseReason = szReason;
		m_open = false;
	}
	virtual void Forget() override { m_open = false; }
	virtual int SendFramed(const uint8_t* data, size_t len, int sendFlags) override
	{
		++m_calls->sends;
		m_calls->lastSendFlags = sendFlags;
		m_calls->lastSend.assign(data, data + len);
		return k_peerSendOK;
	}
	virtual void Flush() override {}
	virtual bool ReceivesPerConnection() const override { return true; }
	virtual int Receive(int, const PeerFrameHandler&) override { return 0; }
	virtual int GetLatency() const override { return 42; }
	virtual PeerRoute GetRoute(bool bPeerConnected) const override { return bPeerConnected ? m_route : PeerRoute::Disconnected; }
	virtual bool IsIPv4() const override { return true; }
	virtual bool GetDetailedStats(std::string&) const override { return false; }

private:
	std::shared_ptr<FakeCalls> m_calls;
	std::string m_label;
	PeerRoute m_route;
	bool m_open = true;
};

static PeerTransportSlot MakeFakeSlot(const std::shared_ptr<FakeCalls>& calls, const char* szLabel, PeerRoute route)
{
	return PeerTransportSlot(std::make_unique<FakeTransport>(calls, szLabel, route));
}

// A restart closes the transport of the attempt before once and keeps the transport of the next attempt open.
static void TestRestartClosesPreviousOnce()
{
	std::shared_ptr<FakeCalls> previousCalls = std::make_shared<FakeCalls>();
	std::shared_ptr<FakeCalls> nextCalls = std::make_shared<FakeCalls>();
	{
		PeerTransportSlot slot = MakeFakeSlot(previousCalls, "previous", PeerRoute::Direct);

		CHECK(slot.CloseIfOpen("new connection being negotiated"));
		CHECK(previousCalls->closes == 1);
		CHECK(previousCalls->lastCloseReason == "new connection being negotiated");

		// A closed transport is not closed twice.
		CHECK(!slot.CloseIfOpen("again"));
		CHECK(previousCalls->closes == 1);

		// The next attempt replaces the connection like the mesh replaces it in its map.
		slot = MakeFakeSlot(nextCalls, "next", PeerRoute::Direct);
		CHECK(previousCalls->closes == 1);
		CHECK(nextCalls->closes == 0);
		CHECK(slot->GetLabel() == "next");
		CHECK(slot->CanSend());
	}

	// Destroying the slot does not close its transport either.
	CHECK(previousCalls->closes == 1);
	CHECK(nextCalls->closes == 0);
}

// Close and send reach the transport of their own connection only.
static void TestCloseAndSendDispatch()
{
	std::shared_ptr<FakeCalls> firstCalls = std::make_shared<FakeCalls>();
	std::shared_ptr<FakeCalls> secondCalls = std::make_shared<FakeCalls>();
	PeerTransportSlot first = MakeFakeSlot(firstCalls, "first", PeerRoute::Direct);
	PeerTransportSlot second = MakeFakeSlot(secondCalls, "second", PeerRoute::Direct);

	const uint8_t packet[] = { 1, 2, 3 };
	CHECK(second->SendFramed(packet, sizeof(packet), k_peerSendReliable) == k_peerSendOK);
	CHECK(secondCalls->sends == 1);
	CHECK(secondCalls->lastSendFlags == k_peerSendReliable);
	CHECK(secondCalls->lastSend == std::vector<uint8_t>(packet, packet + sizeof(packet)));
	CHECK(firstCalls->sends == 0);

	CHECK(first.CloseIfOpen("gone"));
	CHECK(firstCalls->closes == 1);
	CHECK(secondCalls->closes == 0);
	CHECK(!first->CanSend());
	CHECK(second->CanSend());

	PeerTransportSlot empty;
	CHECK(!empty.CloseIfOpen("nothing"));
	CHECK(!empty.Is(PeerTransportKind::Plugin));
	CHECK(first.Is(PeerTransportKind::Plugin));
	CHECK(!first.Is(PeerTransportKind::Gns));
}

// The label and the route of a connection come from its transport.
static void TestLabelAndRouteFromTransport()
{
	std::shared_ptr<FakeCalls> calls = std::make_shared<FakeCalls>();
	PeerTransportSlot relayed = MakeFakeSlot(calls, "relay transport", PeerRoute::Relayed);
	PeerTransportSlot direct = MakeFakeSlot(calls, "direct transport", PeerRoute::Direct);

	CHECK(relayed->GetLabel() == "relay transport");
	CHECK(direct->GetLabel() == "direct transport");
	CHECK(std::strcmp(PeerRouteText(relayed->GetRoute(true)), "Relayed") == 0);
	CHECK(std::strcmp(PeerRouteText(direct->GetRoute(true)), "Direct") == 0);
	CHECK(std::strcmp(PeerRouteText(direct->GetRoute(false)), "(disconnected)") == 0);
	CHECK(std::strcmp(PeerRouteText(PeerRoute::Unknown), "(unknown)") == 0);
}

// The fake plugin table records the calls of the plugin adapter.
namespace FakePlugin
{
	struct Packet
	{
		std::vector<uint8_t> data;
		int64_t sender;
	};

	std::string name = "fakeplugin";
	bool sendResult = true;
	int sends = 0;
	std::string lastSendPlatformID;
	uint64_t lastSendUserID = 0;
	PeerReliability lastReliability = PeerReliability::ReliableUnordered;
	int disconnects = 0;
	std::string lastDisconnectPlatformID;
	uint64_t lastDisconnectUserID = 0;
	bool relayed = false;
	int latency = -1;
	std::vector<Packet> queue;
	int frees = 0;

	std::string GetName() { return name; }
	bool SendPacket(const char* szPlatformUserID, uint64_t userID, const void*, uint32_t, PeerReliability reliability)
	{
		++sends;
		lastSendPlatformID = szPlatformUserID;
		lastSendUserID = userID;
		lastReliability = reliability;
		return sendResult;
	}
	uint32_t GetNextPacketSize() { return queue.empty() ? 0 : static_cast<uint32_t>(queue.front().data.size()); }
	bool ReceivePacket(const uint8_t** ppOutData, uint32_t* pOutNumBytes, int64_t* pOutSenderUserID)
	{
		if (queue.empty())
		{
			return false;
		}
		const size_t size = queue.front().data.size();
		uint8_t* pCopy = new uint8_t[size];
		std::memcpy(pCopy, queue.front().data.data(), size);
		*ppOutData = pCopy;
		*pOutNumBytes = static_cast<uint32_t>(size);
		*pOutSenderUserID = queue.front().sender;
		queue.erase(queue.begin());
		return true;
	}
	void FreePacket(const uint8_t* pData)
	{
		++frees;
		delete[] pData;
	}
	int GetLatency(const char*, uint64_t) { return latency; }
	bool IsRelayed(const char*, uint64_t) { return relayed; }
	void DisconnectPeer(const char* szPlatformUserID, uint64_t userID)
	{
		++disconnects;
		lastDisconnectPlatformID = szPlatformUserID;
		lastDisconnectUserID = userID;
	}

	const PluginPeerTransportApi api = { GetName, SendPacket, GetNextPacketSize, ReceivePacket, FreePacket, GetLatency, IsRelayed, DisconnectPeer };
}

// The plugin adapter maps the reliability flag, addresses the peer by its ids and reads label and route from the plugin.
static void TestPluginAdapter()
{
	PluginPeerTransport transport(FakePlugin::api, 77, "platform-77");
	CHECK(transport.GetKind() == PeerTransportKind::Plugin);
	CHECK(!transport.ReceivesPerConnection());
	CHECK(transport.CanSend());
	CHECK(transport.GetLabel() == "fakeplugin");

	const uint8_t packet[] = { 0, 9 };
	CHECK(transport.SendFramed(packet, sizeof(packet), k_peerSendReliable) == k_peerSendOK);
	CHECK(FakePlugin::lastReliability == PeerReliability::ReliableOrdered);
	CHECK(FakePlugin::lastSendPlatformID == "platform-77");
	CHECK(FakePlugin::lastSendUserID == 77);
	CHECK(transport.SendFramed(packet, sizeof(packet), 0) == k_peerSendOK);
	CHECK(FakePlugin::lastReliability == PeerReliability::UnreliableUnordered);
	FakePlugin::sendResult = false;
	CHECK(transport.SendFramed(packet, sizeof(packet), k_peerSendReliable) == k_peerSendNoConnection);
	FakePlugin::sendResult = true;
	CHECK(FakePlugin::sends == 3);

	FakePlugin::relayed = true;
	CHECK(transport.GetRoute(true) == PeerRoute::Relayed);
	FakePlugin::relayed = false;
	CHECK(transport.GetRoute(true) == PeerRoute::Direct);
	CHECK(transport.GetRoute(false) == PeerRoute::Disconnected);
	FakePlugin::latency = 35;
	CHECK(transport.GetLatency() == 35);
	CHECK(!transport.IsIPv4());
	std::string stats;
	CHECK(!transport.GetDetailedStats(stats));

	// The plugin addresses its peers by user id, a closed or forgotten plugin connection can still send.
	transport.Close("ignored");
	CHECK(FakePlugin::disconnects == 1);
	CHECK(FakePlugin::lastDisconnectPlatformID == "platform-77");
	CHECK(FakePlugin::lastDisconnectUserID == 77);
	transport.Forget();
	CHECK(transport.CanSend());
	CHECK(transport.Receive(255, [](const uint8_t*, uint32_t, int64_t) {}) == 0);
}

// The shared plugin queue hands each packet to the sink with its sender, frees every packet and counts the taken ones.
static void TestPluginSharedReceive()
{
	FakePlugin::queue.clear();
	FakePlugin::frees = 0;
	FakePlugin::queue.push_back({ { 0, 1 }, 5 });
	FakePlugin::queue.push_back({ { 0, 2, 3 }, 6 });
	FakePlugin::queue.push_back({ { 0, 4 }, 5 });
	CHECK(PluginPeerTransport::HasSharedPackets(FakePlugin::api));

	std::vector<int64_t> senders;
	std::vector<uint32_t> sizes;
	// The sink drops the packets of sender 6, as the mesh drops a sender without a plugin connection.
	const int taken = PluginPeerTransport::ReceiveShared(FakePlugin::api, 10,
		[&](int64_t senderUserID, const uint8_t*, uint32_t len, int64_t arrivalMs)
		{
			CHECK(arrivalMs > 0);
			senders.push_back(senderUserID);
			sizes.push_back(len);
			return senderUserID != 6;
		});
	CHECK(taken == 2);
	CHECK(senders == std::vector<int64_t>({ 5, 6, 5 }));
	CHECK(sizes == std::vector<uint32_t>({ 2, 3, 2 }));
	CHECK(FakePlugin::frees == 3);
	CHECK(!PluginPeerTransport::HasSharedPackets(FakePlugin::api));

	// The receive stops at its limit and leaves the rest queued.
	FakePlugin::queue.push_back({ { 0 }, 5 });
	FakePlugin::queue.push_back({ { 0 }, 5 });
	CHECK(PluginPeerTransport::ReceiveShared(FakePlugin::api, 1, [](int64_t, const uint8_t*, uint32_t, int64_t) { return true; }) == 1);
	CHECK(FakePlugin::queue.size() == 1);
	FakePlugin::queue.clear();
}

// The log lines the transports wrote through the sink.
static std::vector<std::string> g_logLines;

static void CaptureLog(PeerLogLevel level, const char* szLine)
{
	g_logLines.push_back(std::string(level == PeerLogLevel::Release ? "R " : "D ") + szLine);
}

static void TestLogSink()
{
	// Without a sink the line is dropped.
	SetPeerTransportLogSink(nullptr);
	PeerTransportLog(PeerLogLevel::Release, "dropped %d", 1);
	CHECK(g_logLines.empty());

	SetPeerTransportLogSink(&CaptureLog);
	PeerTransportLog(PeerLogLevel::Release, "[SIGNAL] user %lld", 42LL);
	PeerTransportLog(PeerLogLevel::Debug, "%s", "debug line");
	CHECK(g_logLines == std::vector<std::string>({ "R [SIGNAL] user 42", "D debug line" }));

	// A long line keeps its text up to the size the network log takes.
	g_logLines.clear();
	const std::string longText(5000, 'x');
	PeerTransportLog(PeerLogLevel::Release, "%s", longText.c_str());
	CHECK(g_logLines.size() == 1 && g_logLines[0] == "R " + longText);
	g_logLines.clear();
}

// A signal channel that records what it sends and hands out the signals a test queued.
class FakeSignalChannel : public IPeerSignalChannel
{
public:
	virtual ~FakeSignalChannel() override {}

	virtual bool IsOpen() const override { return m_open; }
	virtual void SendSignal(int64_t targetUserID, const std::vector<uint8_t>& payload) override
	{
		PeerSignal signal;
		signal.senderUserID = targetUserID;
		signal.payload = payload;
		sent.push_back(signal);
	}
	virtual void DrainSignals(std::deque<PeerSignal>& out) override
	{
		std::deque<PeerSignal> drained = inbox.Drain();
		for (PeerSignal& signal : drained)
		{
			out.push_back(std::move(signal));
		}
	}

	bool m_open = true;
	std::vector<PeerSignal> sent;	///< The user id field holds the target.
	PeerSignalInbox inbox;
};

static void TestSignalOutboxOrderAndCap()
{
	PeerSignalOutbox outbox;
	outbox.Push(7, { 1 });
	outbox.Push(8, { 2, 3 });
	outbox.Push(7, { 4 });

	FakeSignalChannel channel;
	outbox.FlushTo(channel);
	CHECK(channel.sent.size() == 3);
	CHECK(channel.sent.size() == 3 && channel.sent[0].senderUserID == 7 && channel.sent[0].payload == std::vector<uint8_t>({ 1 }));
	CHECK(channel.sent.size() == 3 && channel.sent[1].senderUserID == 8 && channel.sent[1].payload == std::vector<uint8_t>({ 2, 3 }));
	CHECK(channel.sent.size() == 3 && channel.sent[2].senderUserID == 7 && channel.sent[2].payload == std::vector<uint8_t>({ 4 }));
	CHECK(outbox.Size() == 0);

	// A backlog keeps the newest signals, every push over the cap drops the oldest with one log line.
	g_logLines.clear();
	for (int i = 0; i < 200; ++i)
	{
		outbox.Push(i, { static_cast<uint8_t>(i) });
	}
	const size_t kept = PeerSignalOutbox::k_maxBacklog + 1;
	CHECK(outbox.Size() == kept);
	CHECK(g_logLines.size() == 200 - kept);
	CHECK(!g_logLines.empty() && g_logLines[0] == "R Signaling send queue is backed up.  Discarding oldest signals\n");
	channel.sent.clear();
	outbox.FlushTo(channel);
	CHECK(channel.sent.size() == kept);
	CHECK(!channel.sent.empty() && channel.sent.front().senderUserID == 200 - static_cast<int64_t>(kept));
	CHECK(!channel.sent.empty() && channel.sent.back().senderUserID == 199);

	// Clear drops the signals that wait.
	outbox.Push(1, { 1 });
	outbox.Clear();
	CHECK(outbox.Size() == 0);
	g_logLines.clear();
}

static void TestSignalInboxOrderSenderAndCap()
{
	PeerSignalInbox inbox;
	inbox.Push(11, { 1 });
	inbox.Push(12, { 2 });
	std::deque<PeerSignal> drained = inbox.Drain();
	CHECK(drained.size() == 2);
	CHECK(drained.size() == 2 && drained[0].senderUserID == 11 && drained[0].payload == std::vector<uint8_t>({ 1 }));
	CHECK(drained.size() == 2 && drained[1].senderUserID == 12 && drained[1].payload == std::vector<uint8_t>({ 2 }));
	CHECK(inbox.Drain().empty());

	// A full inbox discards its oldest signal for each new one.
	g_logLines.clear();
	for (int i = 0; i < 300; ++i)
	{
		inbox.Push(i, { static_cast<uint8_t>(i) });
	}
	CHECK(g_logLines.size() == 300 - PeerSignalInbox::k_maxSignals);
	CHECK(!g_logLines.empty() && g_logLines[0] == "R [WebSocket] Pending signal queue full (256), discarding oldest signal");
	drained = inbox.Drain();
	CHECK(drained.size() == PeerSignalInbox::k_maxSignals);
	CHECK(!drained.empty() && drained.front().senderUserID == 300 - static_cast<int64_t>(PeerSignalInbox::k_maxSignals));
	CHECK(!drained.empty() && drained.back().senderUserID == 299);

	inbox.Push(1, { 1 });
	inbox.Clear();
	CHECK(inbox.Drain().empty());
	g_logLines.clear();
}

static void TestNetworkConfigMapping()
{
	// The /net override wins over the service value, an unknown engine falls back to WebRTC.
	CHECK(GnsNetworkConfig::ResolveIceImplementation(0, -1) == 0);
	CHECK(GnsNetworkConfig::ResolveIceImplementation(1, -1) == 1);
	CHECK(GnsNetworkConfig::ResolveIceImplementation(1, 2) == 2);
	CHECK(GnsNetworkConfig::ResolveIceImplementation(2, 1) == 1);
	CHECK(GnsNetworkConfig::ResolveIceImplementation(7, -1) == 2);
	CHECK(GnsNetworkConfig::ResolveIceImplementation(-3, -1) == 2);
	CHECK(GnsNetworkConfig::ResolveIceImplementation(1, 9) == 2);

	// Relay only follows the service unless /net forces it.
	CHECK(!GnsNetworkConfig::ResolveRelayOnly(false, -1));
	CHECK(GnsNetworkConfig::ResolveRelayOnly(true, -1));
	CHECK(GnsNetworkConfig::ResolveRelayOnly(false, 1));
	CHECK(GnsNetworkConfig::ResolveRelayOnly(true, 0));

	GnsNetworkConfig config;
	CHECK(config.GetIceEnable() == k_gnsIceEnableAll);
	config.relayOnly = true;
	CHECK(config.GetIceEnable() == k_gnsIceEnableRelay);
	static_assert(k_gnsIceEnableDirectOnly == 6, "A direct step gathers host and server reflexive candidates");

	// The TURN lists name the credentials once for each TURN server.
	config.turnUsername = "user";
	config.turnToken = "token";
	CHECK(config.GetTurnUserList() == "user,user");
	CHECK(config.GetTurnPassList() == "token,token");

	config.firstAttemptTimeouts = { 10000, 45000 };
	config.retryTimeouts = { 45000, 45000 };
	CHECK(config.GetTimeouts(false).initialMs == 10000 && config.GetTimeouts(false).connectedMs == 45000);
	CHECK(config.GetTimeouts(true).initialMs == 45000 && config.GetTimeouts(true).connectedMs == 45000);
}

int main()
{
	TestRestartClosesPreviousOnce();
	TestCloseAndSendDispatch();
	TestLabelAndRouteFromTransport();
	TestPluginAdapter();
	TestPluginSharedReceive();
	TestLogSink();
	TestSignalOutboxOrderAndCap();
	TestSignalInboxOrderSenderAndCap();
	TestNetworkConfigMapping();

	if (g_failures != 0)
	{
		std::printf("%d checks failed\n", g_failures);
		return 1;
	}

	std::printf("All peer transport checks passed\n");
	return 0;
}
