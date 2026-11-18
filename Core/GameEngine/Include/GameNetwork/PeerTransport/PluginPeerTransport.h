#pragma once

#include "GameNetwork/PeerTransport/PeerTransport.h"

#include <cstdint>
#include <functional>
#include <string>

// How a plugin transport carries a packet. The values are those of the plugin API.
enum class PeerReliability : int32_t
{
	UnreliableUnordered = 0,
	ReliableUnordered = 1,
	ReliableOrdered = 2
};

// The calls of the loaded plugin transport. The online provider that hosts the plugin fills the table.
// Every packet travels on the game channel of the plugin.
struct PluginPeerTransportApi
{
	std::string (*getName)();
	bool (*sendPacket)(const char* szPlatformUserID, uint64_t userID, const void* pData, uint32_t numBytes, PeerReliability reliability);
	uint32_t (*getNextPacketSize)();
	/// The buffer belongs to the plugin and goes back through freePacket.
	bool (*receivePacket)(const uint8_t** ppOutData, uint32_t* pOutNumBytes, int64_t* pOutSenderUserID);
	void (*freePacket)(const uint8_t* pData);
	int (*getLatency)(const char* szPlatformUserID, uint64_t userID);
	bool (*isRelayed)(const char* szPlatformUserID, uint64_t userID);
	void (*disconnectPeer)(const char* szPlatformUserID, uint64_t userID);
};

// A peer connection over the plugin transport. The plugin addresses its peers by user id and platform user id.
class PluginPeerTransport : public IPeerTransport
{
public:
	PluginPeerTransport(const PluginPeerTransportApi& api, int64_t userID, const char* szPlatformUserID);
	virtual ~PluginPeerTransport() override {}

	virtual PeerTransportKind GetKind() const override { return PeerTransportKind::Plugin; }
	virtual std::string GetLabel() const override;
	virtual bool CanSend() const override;
	virtual void Close(const char* szReason) override;
	virtual void Forget() override;
	virtual int SendFramed(const uint8_t* data, size_t len, int sendFlags) override;
	virtual void Flush() override;
	virtual bool ReceivesPerConnection() const override { return false; }
	virtual int Receive(int maxFrames, const PeerFrameHandler& handler) override;
	virtual int GetLatency() const override;
	virtual PeerRoute GetRoute(bool bPeerConnected) const override;
	virtual bool IsIPv4() const override;
	virtual bool GetDetailedStats(std::string& text) const override;

	/// Takes a packet of the shared queue with its sender, returns false when the packet was dropped.
	using SharedPacketSink = std::function<bool(int64_t senderUserID, const uint8_t* data, uint32_t len, int64_t arrivalMs)>;
	/// Takes up to maxPackets packets of the queue that every plugin connection shares and hands each to sink.
	/// Returns the number of packets the sink took.
	static int ReceiveShared(const PluginPeerTransportApi& api, int maxPackets, const SharedPacketSink& sink);
	static bool HasSharedPackets(const PluginPeerTransportApi& api);

private:
	const PluginPeerTransportApi* m_pApi;
	int64_t m_userID;
	std::string m_platformUserID;
};
