#pragma once

#include <string>

// The ICE candidate kinds, with the values of k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_*.
constexpr int k_gnsIceEnableRelay = 1;
constexpr int k_gnsIceEnablePrivate = 2;
constexpr int k_gnsIceEnablePublic = 4;
constexpr int k_gnsIceEnableAll = 0x7fffffff;
// The ICE setting of a direct step, host and server reflexive candidates without TURN.
constexpr int k_gnsIceEnableDirectOnly = k_gnsIceEnablePrivate | k_gnsIceEnablePublic;

// The ICE implementations. 0 is the library default, 1 the native client, 2 WebRTC.
constexpr int k_gnsIceImplementationDefault = 0;
constexpr int k_gnsIceImplementationWebRtc = 2;

// The connection timeouts of one connection attempt.
struct GnsTimeouts
{
	int initialMs = 10000;
	int connectedMs = 30000;
};

// The settings of the GameNetworkingSockets transport. The online provider fills it.
struct GnsNetworkConfig
{
	/// Comma separated. Every entry must resolve to a distinct address, or the native ICE client retries duplicates forever.
	std::string stunServers;
	/// True for the debug output of the library, false for its messages only.
	bool verboseLogging = false;

	/// Comma separated, without a "?transport=udp" suffix. The native ICE client takes everything after the host as the port.
	std::string turnServers;
	std::string turnUsername;
	std::string turnToken;

	int iceImplementation = k_gnsIceImplementationWebRtc;
	bool relayOnly = false;	///< The connections use TURN only.

	GnsTimeouts firstAttemptTimeouts;	///< The timeouts of the first attempt of a pair and of the listen socket.
	GnsTimeouts retryTimeouts = { 30000, 30000 };	///< The timeouts of a retry and of a connection during a match.

	/// The k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_* value of a relayed step.
	int GetIceEnable() const { return relayOnly ? k_gnsIceEnableRelay : k_gnsIceEnableAll; }

	// The TURN user and password lists, with the credentials once for each TURN server.
	std::string GetTurnUserList() const { return turnUsername + "," + turnUsername; }
	std::string GetTurnPassList() const { return turnToken + "," + turnToken; }

	const GnsTimeouts& GetTimeouts(bool bRetry) const { return bRetry ? retryTimeouts : firstAttemptTimeouts; }

	/// The ICE implementation of a provider value and an override, an override below 0 means none. An unknown value gives WebRTC.
	static int ResolveIceImplementation(int configured, int overrideValue)
	{
		const int iceImplementation = (overrideValue >= 0) ? overrideValue : configured;
		return (iceImplementation >= k_gnsIceImplementationDefault && iceImplementation <= k_gnsIceImplementationWebRtc) ? iceImplementation : k_gnsIceImplementationWebRtc;
	}

	/// Relay only of a provider value and an override, an override of 1 forces it.
	static bool ResolveRelayOnly(bool configured, int overrideValue)
	{
		return (overrideValue == 1) || configured;
	}
};
