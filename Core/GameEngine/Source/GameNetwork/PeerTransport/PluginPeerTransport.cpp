#include "GameNetwork/PeerTransport/PluginPeerTransport.h"

#include <chrono>

static int64_t NowSteadyMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

PluginPeerTransport::PluginPeerTransport(const PluginPeerTransportApi& api, int64_t userID, const char* szPlatformUserID)
	: m_pApi(&api)
	, m_userID(userID)
	, m_platformUserID(szPlatformUserID)
{
}

std::string PluginPeerTransport::GetLabel() const
{
	return m_pApi->getName();
}

bool PluginPeerTransport::CanSend() const
{
	// The plugin addresses its peers by user id, so a plugin connection has no handle to lose.
	return true;
}

void PluginPeerTransport::Close(const char* szReason)
{
	(void)szReason;
	m_pApi->disconnectPeer(m_platformUserID.c_str(), static_cast<uint64_t>(m_userID));
}

void PluginPeerTransport::Forget()
{
	// The plugin keeps no handle of the connection that the game could drop.
}

int PluginPeerTransport::SendFramed(const uint8_t* data, size_t len, int sendFlags)
{
	// A reliable send stays reliable and ordered on the plugin, every other send is unreliable.
	const PeerReliability reliability = (sendFlags & k_peerSendReliable) != 0
		? PeerReliability::ReliableOrdered
		: PeerReliability::UnreliableUnordered;
	const bool bSent = m_pApi->sendPacket(m_platformUserID.c_str(), static_cast<uint64_t>(m_userID), data, static_cast<uint32_t>(len), reliability);
	return bSent ? k_peerSendOK : k_peerSendNoConnection;
}

void PluginPeerTransport::Flush()
{
	// The plugin sends without a queue that the game could flush.
}

int PluginPeerTransport::Receive(int maxFrames, const PeerFrameHandler& handler)
{
	// The packets of every plugin connection arrive through the shared queue, see ReceiveShared.
	(void)maxFrames;
	(void)handler;
	return 0;
}

int PluginPeerTransport::GetLatency() const
{
	return m_pApi->getLatency(m_platformUserID.c_str(), static_cast<uint64_t>(m_userID));
}

PeerRoute PluginPeerTransport::GetRoute(bool bPeerConnected) const
{
	if (!bPeerConnected)
	{
		return PeerRoute::Disconnected;
	}

	return m_pApi->isRelayed(m_platformUserID.c_str(), static_cast<uint64_t>(m_userID)) ? PeerRoute::Relayed : PeerRoute::Direct;
}

bool PluginPeerTransport::IsIPv4() const
{
	// The plugin does not tell the address family of its connection.
	return false;
}

bool PluginPeerTransport::GetDetailedStats(std::string& text) const
{
	(void)text;
	return false;
}

int PluginPeerTransport::ReceiveShared(const PluginPeerTransportApi& api, int maxPackets, const SharedPacketSink& sink)
{
	int numHandled = 0;
	for (int numTaken = 0; numTaken < maxPackets; ++numTaken)
	{
		if (api.getNextPacketSize() == 0)
		{
			break;
		}

		// the plugin returns its own buffer through pData
		const uint8_t* pData = nullptr;
		uint32_t numBytes = 0;
		int64_t senderUserID = -1;
		if (!api.receivePacket(&pData, &numBytes, &senderUserID) || pData == nullptr)
		{
			// nothing was dequeued, stop instead of spinning forever on the same pending packet
			break;
		}

		// the buffer belongs to the plugin, make sure every exit path below releases it
		struct ScopedPluginPacket
		{
			const PluginPeerTransportApi& m_api;
			const uint8_t* m_pData;
			~ScopedPluginPacket() { m_api.freePacket(m_pData); }
		} scopedPacket{ api, pData };

		// The plugin hands the packet over when the main loop asks, so the receive time is the arrival time.
		const int64_t arrivalMs = NowSteadyMs();

		if (sink(senderUserID, pData, numBytes, arrivalMs))
		{
			++numHandled;
		}
	}

	return numHandled;
}

bool PluginPeerTransport::HasSharedPackets(const PluginPeerTransportApi& api)
{
	return api.getNextPacketSize() > 0;
}
