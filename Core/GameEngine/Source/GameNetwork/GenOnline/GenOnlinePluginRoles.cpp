#include "GameNetwork/GenOnline/GenOnlinePluginRoles.h"
#include <algorithm>
#include <cstddef>

namespace GenOnlinePluginRoles
{
Selection Select(const std::string& anticheatPlugin, const std::vector<std::string>& transportPlugins)
{
	Selection selection;

	if (!anticheatPlugin.empty())
	{
		selection.pluginName = anticheatPlugin;
		selection.bAnticheat = true;
	}
	else
	{
		// Without an external anticheat the first transport plugin loads for its transport only.
		for (const std::string& transport : transportPlugins)
		{
			if (!transport.empty())
			{
				selection.pluginName = transport;
				break;
			}
		}
	}

	if (selection.pluginName.empty())
	{
		return selection;
	}

	for (const std::string& transport : transportPlugins)
	{
		if (transport.empty())
		{
			continue;
		}

		if (transport == selection.pluginName)
		{
			selection.bTransport = true;
		}
		else if (std::find(selection.skippedTransports.begin(), selection.skippedTransports.end(), transport) == selection.skippedTransports.end())
		{
			selection.skippedTransports.push_back(transport);
		}
	}

	return selection;
}

uint32_t GetEnabledCapabilities(const Selection& selection)
{
	if (selection.pluginName.empty())
	{
		return 0;
	}

	uint32_t capabilities = GENONLINE_PLUGIN_CAP_LINKED_ACCOUNT;
	if (selection.bAnticheat)
	{
		capabilities |= GENONLINE_PLUGIN_CAP_ANTICHEAT;
	}

	if (selection.bTransport)
	{
		capabilities |= GENONLINE_PLUGIN_CAP_TRANSPORT;
	}

	return capabilities;
}

bool DoesPluginTrackPeersOnly(const Selection& selection)
{
	return !selection.pluginName.empty() && !selection.bAnticheat && selection.bTransport;
}

bool DoesLoadFailureBlockOnline(const Selection& selection)
{
	return !selection.pluginName.empty() && selection.bAnticheat;
}

bool DoesTransportNameSenders(const GenOnlinePluginTransportApi& transport)
{
	const size_t requiredSize = offsetof(GenOnlinePluginTransportApi, ReceivePacketFrom) + sizeof(transport.ReceivePacketFrom);
	return transport.structSize >= requiredSize && transport.ReceivePacketFrom != nullptr;
}

bool DoesTransportLossFailLoad(const Selection& selection)
{
	return !selection.pluginName.empty() && !selection.bAnticheat && selection.bTransport;
}
}
