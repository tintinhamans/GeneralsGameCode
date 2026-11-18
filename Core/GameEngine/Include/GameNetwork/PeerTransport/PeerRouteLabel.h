#pragma once

#include "GameNetwork/PeerTransport/PeerTransport.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// How far a peer connection got, as the user interface tells it apart.
enum class PeerLinkState
{
	NotConnected,
	Connecting,
	Connected
};

/// The route of a peer for the user interface, such as "GNS direct" or "EOS relay".
/// A peer without a known route shows "connecting" or "not connected".
std::string PeerRouteLabel(const std::string& transportLabel, PeerRoute route, PeerLinkState linkState);

/// The route label with the IP version of a GNS peer, such as "GNS direct IPv4". A plugin does not tell its IP version.
std::string PeerPathLabel(const std::string& transportLabel, PeerTransportKind kind, bool bIPv4, PeerRoute route, PeerLinkState linkState);

// What a /net command asks for.
enum class PeerNetAction
{
	Status,	///< /net shows the settings of the next lobby and the route to each player.
	SetRelay,	///< /net relay auto | force, the value is the relay override.
	RelayUsage,
	SetGnsIce,	///< /net gns ice auto | native | webrtc, the value is the ICE implementation override.
	GnsIceUsage,
	SetTransport,	///< /net transport auto | gns, the value is the transport override.
	TransportUsage,	///< /net transport with another value or none, the word holds the value.
	Reset,	///< /net reset clears every override.
	Usage
};

struct PeerNetCommand
{
	PeerNetAction action = PeerNetAction::Usage;
	int value = -1;	///< The override of a set action, -1 means auto.
	std::string word;	///< The lower case value of a transport usage action, empty when none was given.
};

/// Parses the lower case words after /net. The subcommand is the first word, args the rest of the line.
/// The value of /net transport is case insensitive.
PeerNetCommand ParsePeerNetCommand(const std::string& subcommand, const std::string& args);

/// The plugin transports this client offers to the service. A transport override of 1 offers none, so every pair uses GNS.
/// Any other override offers every available one.
std::vector<std::string> FilterOfferedTransports(const std::vector<std::string>& available, int transportOverride);

/// The relay policy of a plugin transport for the resolved relay only setting that GNS also uses.
/// 1 forces the relay of the plugin and 0 allows direct connections, the values of GENONLINE_PLUGIN_RELAY_FORCE and GENONLINE_PLUGIN_RELAY_ALLOW.
int PluginRelayPolicy(bool bRelayOnly);

/// True when a plugin table that reports structSize bytes holds the whole field at fieldOffset of fieldSize bytes.
bool PluginTableCoversField(uint32_t structSize, size_t fieldOffset, size_t fieldSize);
