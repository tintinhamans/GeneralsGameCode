#pragma once

#include "GameNetwork/PeerTransport/GnsNetworkConfig.h"
#include "GameNetwork/PeerTransport/PeerSignalChannel.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <steam/steamnetworkingtypes.h>

class GnsPeerNetwork;
class GnsSignalingClient;

// A peer connection that the status callback found by its handle.
struct GnsPeerRef
{
	int64_t key = -1;	///< The key the listener files the connection under.
	int64_t userID = -1;	///< The user the connection is for.
	HSteamNetConnection hConnection = k_HSteamNetConnection_Invalid;	///< The handle the connection holds.
};

// The states a GameNetworkingSockets connection reports on its way up.
enum class GnsPeerState
{
	Connecting,
	FindingRoute,
	Connected
};

// How a connection ended. The library already closed our end.
struct GnsPeerEnd
{
	int endReason = 0;	///< The ESteamNetConnectionEnd of the connection.
	bool closedOnPurpose = false;	///< The peer closed it on purpose, not to negotiate a new connection and not as a rejection.
	bool wasError = false;	///< The connection failed, it did not close cleanly.
};

// The settings of an incoming connection of a pair before it is accepted.
struct GnsIncomingSetup
{
	bool retryTimeouts = false;	///< The connection takes the timeouts of a retry.
	bool directOnly = false;	///< The step of the pair is direct, the connection drops the TURN servers of the listen socket.
	int stepAttempt = 0;
};

// Takes the connection changes of the library on the main thread. The online provider implements it and keeps the
// policy: its connection map, retries, the connection order and the lobby.
class IGnsPeerListener
{
public:
	virtual ~IGnsPeerListener() {}

	/// The network that takes the status changes, nullptr while there is none. The change is ignored then.
	virtual GnsPeerNetwork* GetActiveNetwork() = 0;
	/// Finds the connection that holds the handle, false when none does.
	virtual bool FindConnection(HSteamNetConnection hConnection, GnsPeerRef& peer) = 0;
	/// The connection closed or failed.
	virtual void OnConnectionEnded(const GnsPeerRef& peer, const GnsPeerEnd& end) = 0;
	/// An incoming connection of the peer passed the identity check. Returns false when the listener rejected it,
	/// it closed the connection then.
	virtual bool OnIncomingConnection(const GnsPeerRef& peer, HSteamNetConnection hConnection, const char* szDescription) = 0;
	virtual GnsIncomingSetup GetIncomingSetup(const GnsPeerRef& peer) = 0;
	virtual void OnStateChanged(const GnsPeerRef& peer, GnsPeerState state) = 0;
};

// Owns the process-wide GameNetworkingSockets library lifetime.
class GnsPeerLibrary
{
public:
	// Inits once with userID's identity; stays up until Shutdown (a new login always follows a full teardown).
	// Takes the STUN servers and the log level of config. The listener gets the connection changes until Shutdown.
	// Returns false on failure; caller must not create a listen socket/connection in that case.
	static bool EnsureInitialized(int64_t userID, const GnsNetworkConfig& config, IGnsPeerListener* pListener);

	// Tears the library down. Safe to call when not initialized.
	static void Shutdown();

	static bool IsInitialized() { return s_bInitialized; }

	// Drives SteamNetworkingSockets()->RunCallbacks(), independent of the network lifetime.
	static void Tick();

private:
	static bool s_bInitialized;
};

// The GameNetworkingSockets side of one lobby: the listen socket, the custom signalling and the connection settings.
class GnsPeerNetwork
{
public:
	GnsPeerNetwork();
	~GnsPeerNetwork();

	GnsPeerNetwork(const GnsPeerNetwork&) = delete;
	GnsPeerNetwork& operator=(const GnsPeerNetwork&) = delete;

	/// Creates the signalling client over the channel and the listen socket. The channel must outlive the network.
	bool Init(const GnsNetworkConfig& config, IPeerSignalChannel* pChannel);

	const GnsNetworkConfig& GetConfig() const { return m_config; }
	HSteamListenSocket GetListenSocket() const { return m_hListenSock; }

	/// Sets the TURN credentials of the next connections and of the listen socket.
	void SetTurnCredentials(const std::string& strUsername, const std::string& strToken);

	/// Starts a connection to the user over custom signalling, invalid on failure.
	HSteamNetConnection Connect(int64_t remoteUserID, int virtualPortRemote, bool bRelay, bool bRetryTimeouts);

	/// Applies the timeouts and the ICE settings of the pair to an incoming connection before it is accepted.
	void ApplyIncomingSetup(HSteamNetConnection hConnection, int64_t userID, const GnsIncomingSetup& setup);

	/// Keeps an incoming connection of a user that has no connection on this side yet, a newer one replaces it.
	void HoldIncoming(int64_t userID, HSteamNetConnection hConnection);
	/// Removes a held incoming connection that closed.
	void ForgetHeldIncoming(HSteamNetConnection hConnection);
	/// Hands over the held incoming connection of the user, invalid when there is none.
	HSteamNetConnection TakeHeldIncoming(int64_t userID);
	/// Accepts a held incoming connection with the setup of the pair.
	void AcceptHeldIncoming(HSteamNetConnection hConnection, int64_t userID, const GnsIncomingSetup& setup);

	/// Sends the queued signals and dispatches the received ones. Call it on the main thread.
	void Poll();

	void CloseListenSocket();

	static void CloseConnection(HSteamNetConnection hConnection, int reason, const char* szDebug);

private:
	bool CreateListenSocket();

	GnsNetworkConfig m_config;
	std::unique_ptr<GnsSignalingClient> m_pSignaling;
	HSteamListenSocket m_hListenSock = k_HSteamListenSocket_Invalid;
	std::map<int64_t, HSteamNetConnection> m_heldIncoming;	///< Incoming connections of users this side has not started yet.
};
