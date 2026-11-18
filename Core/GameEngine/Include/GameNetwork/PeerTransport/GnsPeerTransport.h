#pragma once

#include "GameNetwork/PeerTransport/PeerTransport.h"

#include <steam/steamnetworkingtypes.h>

// A peer connection over GameNetworkingSockets. It holds the connection handle, which is invalid before the connection
// exists and after it is gone.
class GnsPeerTransport : public IPeerTransport
{
public:
	GnsPeerTransport(int64_t userID, HSteamNetConnection hConnection);
	virtual ~GnsPeerTransport() override {}

	HSteamNetConnection GetHandle() const { return m_hConnection; }
	void SetHandle(HSteamNetConnection hConnection) { m_hConnection = hConnection; }

	/// Names the connection after the user in the GameNetworkingSockets logs.
	static void NameConnection(HSteamNetConnection hConnection, int64_t userID);

	virtual PeerTransportKind GetKind() const override { return PeerTransportKind::Gns; }
	virtual std::string GetLabel() const override;
	virtual bool CanSend() const override;
	virtual void Close(const char* szReason) override;
	virtual void Forget() override;
	virtual int SendFramed(const uint8_t* data, size_t len, int sendFlags) override;
	virtual void Flush() override;
	virtual bool ReceivesPerConnection() const override { return true; }
	virtual int Receive(int maxFrames, const PeerFrameHandler& handler) override;
	virtual int GetLatency() const override;
	virtual PeerRoute GetRoute(bool bPeerConnected) const override;
	virtual bool IsIPv4() const override;
	virtual bool GetDetailedStats(std::string& text) const override;

private:
	static int64_t GetArrivalMs(const SteamNetworkingMessage_t* msg);	///< transport receive time in steady_clock ms

	int64_t m_userID;
	HSteamNetConnection m_hConnection;
};
