#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <curl/curl.h>
#include "GameNetwork/GeneralsOnline/ServiceEndpointPolicy.h"

namespace ServiceConnection
{
struct ProxyRoute
{
	std::string url;
	bool direct = true;
	bool allowIntegratedAuth = false;
};

struct ResolvedProxyPolicy
{
	std::vector<ProxyRoute> routes;
	std::string error;
	unsigned long nativeError = 0;
	std::string failurePhase;
	std::string fallbackReason;
};

struct ProxyResolution
{
	std::atomic<bool> ready{false};
	ResolvedProxyPolicy result;
};
// Successful system policies expire after 60 seconds; settings saves invalidate immediately.
void InvalidateProxyPolicyCache();
bool ShouldLogProxyPolicy(bool websocket, const ResolvedProxyPolicy& result);
std::shared_ptr<ProxyResolution> StartProxyResolution(ServiceConnectionSettings settings, std::string destinationURL);

// Worker-only: PAC/WPAD resolution can take up to five seconds. Only absent optional WPAD permits system fallback.
ResolvedProxyPolicy ResolveProxyPolicy(const ServiceConnectionSettings& settings, const std::string& destinationURL);
bool ValidateProxyURL(const std::string& proxyURL, std::string& error);
// Configures policy only. The caller owns URL, transfer deadlines and ordered route retries.
bool ConfigureCurl(CURL* curl, const std::string& destinationURL, const ProxyRoute& route, std::string& error);
bool SaveProxyCredentials(const std::string& proxyURL, const std::string& username,
	const std::string& password, std::string& error);
bool DeleteProxyCredentials(const std::string& proxyURL, std::string& error);
const char* ProtocolName(long version);
const char* FailurePhase(CURLcode code, long status, long proxyStatus);
}