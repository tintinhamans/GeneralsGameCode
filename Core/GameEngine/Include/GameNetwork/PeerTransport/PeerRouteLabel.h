#pragma once

#include "GameNetwork/PeerTransport/PeerTransport.h"

#include <string>

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
	Reset,	///< /net reset clears both overrides.
	Usage
};

struct PeerNetCommand
{
	PeerNetAction action = PeerNetAction::Usage;
	int value = -1;	///< The override of a set action, -1 means auto.
};

/// Parses the lower case words after /net. The subcommand is the first word, args the rest of the line.
PeerNetCommand ParsePeerNetCommand(const std::string& subcommand, const std::string& args);
