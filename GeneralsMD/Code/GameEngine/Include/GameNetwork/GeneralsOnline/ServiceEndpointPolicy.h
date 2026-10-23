#pragma once
#include <string>
#include <vector>
#include <curl/curl.h>

struct ServiceConnectionSettings
{
	// automatic, primary, alternative or custom
	std::string endpointMode = "automatic";
	// https origin of the custom server, empty unless endpointMode is custom
	std::string customEndpoint;
	std::string proxyMode = "system";
	std::string proxyURL;
	bool integratedProxyAuth = false;
};

namespace ServiceConnection
{
// host[:port] or https://host[:port] to its origin (https, lower case host, default port dropped); false with a reason for anything else
bool NormalizeCustomEndpoint(const std::string& typed, std::string& origin, std::string& error);
// The key the login token is stored under: one for every GO route, one per custom origin, empty when a custom server has no valid address
std::string CredentialServerKey(const ServiceConnectionSettings& settings);
std::vector<std::string> BootstrapRoutes(const std::string& environment,
    const ServiceConnectionSettings& settings, const std::string& remembered);
bool IsTrustedServiceURL(const std::string& url, const std::string& base, bool websocket);
std::string RequestURLForLog(const std::string& url, const std::string& serviceBase);
bool IsExpectedVersionManifest(const std::string& body);
// Verified TLS (not in _DEBUG builds), no redirects, no embedded credentials, direct connection; HTTPS or WSS only.
bool ConfigureServiceTransport(CURL* curl, const std::string& url, std::string& error);
}
