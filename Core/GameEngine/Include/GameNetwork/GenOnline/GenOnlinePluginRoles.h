#pragma once
#include <GenOnlinePluginApi.h>
#include <cstdint>
#include <string>
#include <vector>

// Decides which external plugin DLL the plugin host loads and for which role, from plugins.anticheat and plugins.transport of client.json.
namespace GenOnlinePluginRoles
{
	struct Selection
	{
		// Empty when no external plugin loads.
		std::string pluginName;

		// True when the plugin is the anticheat provider. GenOnline Anti-Cheat is the anticheat otherwise.
		bool bAnticheat = false;

		bool bTransport = false;

		// Transport plugins that do not load because the host loads one external plugin.
		std::vector<std::string> skippedTransports;
	};

	// anticheatPlugin is the external anticheat plugin, empty for the built-in GenOnline Anti-Cheat. The external anticheat
	// plugin loads when there is one, else the first transport plugin loads for its transport only.
	Selection Select(const std::string& anticheatPlugin, const std::vector<std::string>& transportPlugins);

	// The GENONLINE_PLUGIN_CAP_ bits that the host passes to the plugin in GenOnlinePluginHostApi.enabledCapabilities.
	uint32_t GetEnabledCapabilities(const Selection& selection);

	// True when the plugin gets the session and peer calls only so that its transport knows its peers. GenOnline Anti-Cheat
	// answers those calls as the anticheat.
	bool DoesPluginTrackPeersOnly(const Selection& selection);

	// True when a plugin that fails to load blocks online play. A transport only plugin that fails leaves the standard connection.
	bool DoesLoadFailureBlockOnline(const Selection& selection);

	// True when the transport table reaches ReceivePacketFrom and fills it, so every packet names its sender. The host uses
	// no other transport.
	bool DoesTransportNameSenders(const GenOnlinePluginTransportApi& transport);

	// True when a plugin that offers no usable transport takes the failed load path. The anticheat plugin keeps its other roles.
	bool DoesTransportLossFailLoad(const Selection& selection);
}
