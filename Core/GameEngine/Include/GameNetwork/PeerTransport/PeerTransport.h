#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

// The transport kinds a peer connection can run on.
enum class PeerTransportKind
{
	Gns,	///< GameNetworkingSockets, built into the game.
	Plugin	///< A transport of the loaded plugin.
};

// How the packets of a peer connection travel.
enum class PeerRoute
{
	Disconnected,
	Unknown,	///< The transport could not tell.
	Direct,
	Relayed
};

enum class PeerLogLevel
{
	Debug,
	Release
};

// The send flag of a reliable packet, with the value of the GameNetworkingSockets flag.
constexpr int k_peerSendReliable = 8;
// The send results, with the values of the GameNetworkingSockets EResult.
constexpr int k_peerSendOK = 1;
constexpr int k_peerSendNoConnection = 3;

// Writes a line to the network log. The online provider implements it.
void PeerTransportLog(PeerLogLevel level, const char* fmt, ...);

// Takes one received packet that starts with its channel byte, with its arrival time in steady_clock ms.
using PeerFrameHandler = std::function<void(const uint8_t* data, uint32_t len, int64_t arrivalMs)>;

// The transport of one peer connection.
class IPeerTransport
{
public:
	virtual ~IPeerTransport() {}

	virtual PeerTransportKind GetKind() const = 0;
	virtual std::string GetLabel() const = 0;	///< The name of the transport for logs and the user interface.

	/// True when the transport can take a packet. A closed transport says no.
	virtual bool CanSend() const = 0;
	virtual void Close(const char* szReason) = 0;
	/// Drops the connection without closing it, the transport can no longer send then.
	virtual void Forget() = 0;
	/// Sends a packet that starts with its channel byte, returns k_peerSendOK on success.
	virtual int SendFramed(const uint8_t* data, size_t len, int sendFlags) = 0;
	virtual void Flush() = 0;

	/// False when the packets of the transport arrive through one queue that all its connections share.
	virtual bool ReceivesPerConnection() const = 0;
	/// Hands up to maxFrames received packets to handler, returns the number taken or -1 without a connection.
	virtual int Receive(int maxFrames, const PeerFrameHandler& handler) = 0;

	virtual int GetLatency() const = 0;	///< The round trip in ms, -1 when unknown.
	virtual PeerRoute GetRoute(bool bPeerConnected) const = 0;
	virtual bool IsIPv4() const = 0;
	/// The detailed status of the transport, false without a connection.
	virtual bool GetDetailedStats(std::string& text) const = 0;
};

// The text a route shows in logs and the user interface.
inline const char* PeerRouteText(PeerRoute route)
{
	switch (route)
	{
	case PeerRoute::Direct:
		return "Direct";
	case PeerRoute::Relayed:
		return "Relayed";
	case PeerRoute::Disconnected:
		return "(disconnected)";
	default:
		return "(unknown)";
	}
}

// Holds the transport of one peer connection. Replacing or destroying the transport never closes it.
class PeerTransportSlot
{
public:
	PeerTransportSlot() {}
	explicit PeerTransportSlot(std::unique_ptr<IPeerTransport> transport)
		: m_transport(std::move(transport))
	{
	}

	IPeerTransport* Get() const { return m_transport.get(); }
	IPeerTransport* operator->() const { return m_transport.get(); }

	bool Is(PeerTransportKind kind) const
	{
		return m_transport != nullptr && m_transport->GetKind() == kind;
	}

	/// Closes the connection of the transport when it can send, true when it closed one.
	bool CloseIfOpen(const char* szReason)
	{
		if (m_transport == nullptr || !m_transport->CanSend())
		{
			return false;
		}

		m_transport->Close(szReason);
		return true;
	}

private:
	std::unique_ptr<IPeerTransport> m_transport;
};
