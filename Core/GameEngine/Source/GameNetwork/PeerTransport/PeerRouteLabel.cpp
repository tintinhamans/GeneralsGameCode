#include "GameNetwork/PeerTransport/PeerRouteLabel.h"

#include <cctype>
#include <sstream>
#include <vector>

std::string PeerRouteLabel(const std::string& transportLabel, PeerRoute route, PeerLinkState linkState)
{
	if (linkState == PeerLinkState::NotConnected)
	{
		return "not connected";
	}
	if (linkState == PeerLinkState::Connecting)
	{
		return "connecting";
	}

	const char* szRoute = nullptr;
	switch (route)
	{
	case PeerRoute::Direct:
		szRoute = "direct";
		break;
	case PeerRoute::Relayed:
		szRoute = "relay";
		break;
	case PeerRoute::Disconnected:
		return "not connected";
	default:
		return "connecting";
	}

	// The plugin names its transport in lower case, the user interface shows it as an acronym.
	std::string label;
	for (char c : transportLabel)
	{
		label += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	}

	if (label.empty())
	{
		return szRoute;
	}
	return label + " " + szRoute;
}

std::string PeerPathLabel(const std::string& transportLabel, PeerTransportKind kind, bool bIPv4, PeerRoute route, PeerLinkState linkState)
{
	std::string label = PeerRouteLabel(transportLabel, route, linkState);
	const bool bHasRoute = (linkState == PeerLinkState::Connected) && (route == PeerRoute::Direct || route == PeerRoute::Relayed);
	if (kind == PeerTransportKind::Gns && bHasRoute)
	{
		label += bIPv4 ? " IPv4" : " IPv6";
	}
	return label;
}

PeerNetCommand ParsePeerNetCommand(const std::string& subcommand, const std::string& args)
{
	std::vector<std::string> words;
	std::istringstream stream(args);
	std::string word;
	while (stream >> word)
	{
		words.push_back(word);
	}
	const std::string first = words.empty() ? std::string() : words[0];

	PeerNetCommand command;
	if (subcommand.empty())
	{
		command.action = PeerNetAction::Status;
		return command;
	}

	if (subcommand == "reset")
	{
		command.action = words.empty() ? PeerNetAction::Reset : PeerNetAction::Usage;
		return command;
	}

	if (subcommand == "transport")
	{
		std::string value;
		for (char c : first)
		{
			value += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}

		command.action = PeerNetAction::SetTransport;
		if (value == "gns")
		{
			command.value = 1;
		}
		else if (value == "auto")
		{
			command.value = -1;
		}
		else
		{
			command.action = PeerNetAction::TransportUsage;
			command.word = value;
		}
		return command;
	}

	if (subcommand == "relay")
	{
		command.action = PeerNetAction::SetRelay;
		if (first == "force")
		{
			command.value = 1;
		}
		else if (first == "auto")
		{
			command.value = -1;
		}
		else
		{
			command.action = PeerNetAction::RelayUsage;
		}
		return command;
	}

	if (subcommand == "gns")
	{
		command.action = PeerNetAction::GnsIceUsage;
		if (first != "ice" || words.size() < 2)
		{
			return command;
		}

		command.action = PeerNetAction::SetGnsIce;
		if (words[1] == "native")
		{
			command.value = 1;
		}
		else if (words[1] == "webrtc")
		{
			command.value = 2;
		}
		else if (words[1] == "auto")
		{
			command.value = -1;
		}
		else
		{
			command.action = PeerNetAction::GnsIceUsage;
		}
		return command;
	}

	command.action = PeerNetAction::Usage;
	return command;
}

std::vector<std::string> FilterOfferedTransports(const std::vector<std::string>& available, int transportOverride)
{
	if (transportOverride == 1)
	{
		return std::vector<std::string>();
	}
	return available;
}

int PluginRelayPolicy(bool bRelayOnly)
{
	return bRelayOnly ? 1 : 0;
}

bool PluginTableCoversField(uint32_t structSize, size_t fieldOffset, size_t fieldSize)
{
	return static_cast<size_t>(structSize) >= fieldOffset + fieldSize;
}
