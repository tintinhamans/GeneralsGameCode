#pragma once
#include <string>
#include <vector>
#include <curl/curl.h>

struct ServiceConnectionSettings
{
	std::string endpointMode = "automatic";
	std::string proxyMode = "system";
	std::string proxyURL;
	bool integratedProxyAuth = false;
};

namespace ServiceConnection
{
std::vector<std::string> BootstrapRoutes(const std::string& environment,
    const ServiceConnectionSettings& settings, const std::string& remembered);
bool IsTrustedServiceURL(const std::string& url, const std::string& base, bool websocket);
std::string RequestURLForLog(const std::string& url, const std::string& serviceBase);
bool IsExpectedVersionManifest(const std::string& body);
// Verified TLS (not in _DEBUG builds), no redirects, no embedded credentials, direct connection; HTTPS or WSS only.
bool ConfigureServiceTransport(CURL* curl, const std::string& url, std::string& error);
}
