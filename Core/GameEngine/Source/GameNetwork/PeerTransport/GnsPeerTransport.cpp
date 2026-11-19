#include "GameNetwork/PeerTransport/GnsPeerTransport.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <iterator>
#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>

static_assert(k_peerSendReliable == k_nSteamNetworkingSend_Reliable, "The reliable send flag must match GameNetworkingSockets");
static_assert(k_peerSendOK == k_EResultOK, "The send results must match EResult");
static_assert(k_peerSendNoConnection == k_EResultNoConnection, "The send results must match EResult");

// The messages one receive takes from GameNetworkingSockets at most.
static constexpr int k_maxReceiveBatch = 255;

static int64_t NowSteadyMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

GnsPeerTransport::GnsPeerTransport(int64_t userID, HSteamNetConnection hConnection)
	: m_userID(userID)
	, m_hConnection(hConnection)
{
}

void GnsPeerTransport::NameConnection(HSteamNetConnection hConnection, int64_t userID)
{
	if (SteamNetworkingSockets())
	{
		SteamNetworkingSockets()->SetConnectionName(hConnection, std::format("Steam Connection User{}", userID).c_str());
	}
}

std::string GnsPeerTransport::GetLabel() const
{
	return "GNS";
}

bool GnsPeerTransport::CanSend() const
{
	return m_hConnection != k_HSteamNetConnection_Invalid;
}

void GnsPeerTransport::Close(const char* szReason)
{
	if (SteamNetworkingSockets())
	{
		SteamNetworkingSockets()->CloseConnection(m_hConnection, 0, szReason, false);
	}
}

void GnsPeerTransport::Forget()
{
	m_hConnection = k_HSteamNetConnection_Invalid;
}

int GnsPeerTransport::SendFramed(const uint8_t* data, size_t len, int sendFlags)
{
	return static_cast<int>(SteamNetworkingSockets()->SendMessageToConnection(m_hConnection, data, static_cast<uint32>(len), sendFlags, nullptr));
}

void GnsPeerTransport::Flush()
{
	SteamNetworkingSockets()->FlushMessagesOnConnection(m_hConnection);
}

int GnsPeerTransport::Receive(int maxFrames, const PeerFrameHandler& handler)
{
	SteamNetworkingMessage_t* pMsg[k_maxReceiveBatch] = { nullptr };
	int numPackets = -1;
	if (m_hConnection != k_HSteamNetConnection_Invalid)
	{
		numPackets = SteamNetworkingSockets()->ReceiveMessagesOnConnection(m_hConnection, pMsg, (std::min)(maxFrames, k_maxReceiveBatch));
		PeerTransportLog(PeerLogLevel::Debug, "[DISC] Recv Result %d from user %lld", numPackets, m_userID);
	}
	else
	{
		PeerTransportLog(PeerLogLevel::Debug, "[DISC] Recv Failed 1 from user %lld", m_userID);
	}

	if (numPackets <= 0)
	{
		return numPackets;
	}

	if (numPackets > static_cast<int>(std::size(pMsg)))
	{
		PeerTransportLog(PeerLogLevel::Release,
			"Game Packet Recv: numPackets (%d) > pMsg capacity (%zu), clamping",
			numPackets, std::size(pMsg));
		numPackets = static_cast<int>(std::size(pMsg));
	}

	for (int iPacket = 0; iPacket < numPackets; ++iPacket)
	{
		SteamNetworkingMessage_t* msg = pMsg[iPacket];
		if (!msg)
		{
			// CRITICAL BUG FIX: Don't return early - continue loop to release remaining messages
			// Skipping null entry but continue processing others
			continue;
		}

		handler(static_cast<const uint8_t*>(msg->GetData()), static_cast<uint32_t>(msg->m_cbSize), GetArrivalMs(msg));
		msg->Release();
	}

	return numPackets;
}

int GnsPeerTransport::GetLatency() const
{
	// TODO_STEAM: consider using lanes
	if (m_hConnection != k_HSteamNetConnection_Invalid)
	{
		const int k_nLanes = 1;
		SteamNetConnectionRealTimeStatus_t status;
		SteamNetConnectionRealTimeLaneStatus_t laneStatus[k_nLanes];

		EResult res = SteamNetworkingSockets()->GetConnectionRealTimeStatus(m_hConnection, &status, k_nLanes, laneStatus);
		if (res == k_EResultOK)
		{
			return status.m_nPing;
		}
	}

	return -1;
}

PeerRoute GnsPeerTransport::GetRoute(bool bPeerConnected) const
{
	(void)bPeerConnected;
	if (m_hConnection == k_HSteamNetConnection_Invalid)
	{
		return PeerRoute::Disconnected;
	}

	SteamNetConnectionInfo_t info;
	if (!SteamNetworkingSockets()->GetConnectionInfo(m_hConnection, &info))
	{
		PeerTransportLog(PeerLogLevel::Debug, "[STEAM] PlayerConnection::GetConnectionType failed to get connection info");
		return PeerRoute::Unknown;
	}

	return (info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Relayed) != 0 ? PeerRoute::Relayed : PeerRoute::Direct;
}

bool GnsPeerTransport::IsIPv4() const
{
	if (m_hConnection == k_HSteamNetConnection_Invalid)
	{
		return false;
	}

	SteamNetConnectionInfo_t info;
	SteamNetworkingSockets()->GetConnectionInfo(m_hConnection, &info);

	return info.m_addrRemote.IsIPv4();
}

bool GnsPeerTransport::GetDetailedStats(std::string& text) const
{
	if (m_hConnection == k_HSteamNetConnection_Invalid)
	{
		return false;
	}

	char szBuf[2048] = { 0 };
	int ret = SteamNetworkingSockets()->GetDetailedConnectionStatus(m_hConnection, szBuf, 2048);

	PeerTransportLog(PeerLogLevel::Release, "[STEAM] PlayerConnection::GetStats returned %d", ret);
	text = szBuf;
	return true;
}

int64_t GnsPeerTransport::GetArrivalMs(const SteamNetworkingMessage_t* msg)
{
	const int64_t nowMs = NowSteadyMs();
	if (msg == nullptr || SteamNetworkingUtils() == nullptr || msg->m_usecTimeReceived <= 0)
	{
		return nowMs;
	}

	// how long the message waited in the transport's queue for the main loop
	const int64_t queuedMs = (SteamNetworkingUtils()->GetLocalTimestamp() - msg->m_usecTimeReceived) / 1000;
	return nowMs - (std::max)(static_cast<int64_t>(0), queuedMs);
}
