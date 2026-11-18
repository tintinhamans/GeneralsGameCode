// Tests the per-peer transport layer without GameNetworkingSockets: the slot that holds the transport of a connection,
// the route text and the plugin adapter over a fake plugin table.

#include "GameNetwork/PeerTransport/PeerTransport.h"
#include "GameNetwork/PeerTransport/PluginPeerTransport.h"

#include <cstdio>
#include <cstring>
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

int main()
{
	TestRestartClosesPreviousOnce();
	TestCloseAndSendDispatch();
	TestLabelAndRouteFromTransport();
	TestPluginAdapter();
	TestPluginSharedReceive();

	if (g_failures != 0)
	{
		std::printf("%d checks failed\n", g_failures);
		return 1;
	}

	std::printf("All peer transport checks passed\n");
	return 0;
}
