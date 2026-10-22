#pragma once
#include <string>
#include <vector>
#include <curl/curl.h>

struct ServiceConnectionSettings
{
	std::string endpointMode = "automatic";
};

namespace ServiceConnection
{
std::vector<std::string> BootstrapRoutes(const std::string& environment,
    const ServiceConnectionSettings& settings, const std::string& remembered);
bool IsTrustedServiceURL(const std::string& url, const std::string& base, bool websocket);
bool IsExpectedVersionManifest(const std::string& body);
// Verified TLS, no redirects, no embedded credentials, direct connection; HTTPS or WSS only.
bool ConfigureServiceTransport(CURL* curl, const std::string& url, std::string& error);
}
