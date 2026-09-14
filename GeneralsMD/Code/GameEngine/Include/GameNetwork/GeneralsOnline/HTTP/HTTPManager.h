#pragma once

#include "HTTPRequest.h"
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <winhttp.h>
#include "../NGMP_include.h"

#pragma comment(lib, "winhttp.lib")

enum class EHTTPVerb
{
	HTTP_VERB_GET,
	HTTP_VERB_POST,
	HTTP_VERB_PUT,
	HTTP_VERB_DELETE
};

// NOTE: WinHTTP's high-level WinHttpConnect API doesn't offer a clean way to
// force a specific IP family without breaking TLS SNI, so FORCE_IPV4/FORCE_IPV6
// are accepted for API compatibility but not acted upon -- WinHTTP's normal
// DNS resolution decides. DONT_CARE remains the effective behavior always.
enum class EIPProtocolVersion
{
	DONT_CARE = 0,
	FORCE_IPV4 = 4,
	FORCE_IPV6 = 6
};

class HTTPManager
{
public:
	HTTPManager() noexcept;
	~HTTPManager();

	void Initialize();

	void Tick();

	HINTERNET GetSessionHandle() const { return m_hSession; }

	void SendGETRequest(const char* szURI, EIPProtocolVersion protover, std::map<std::string, std::string>& inHeaders, std::function<void(bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)> completionCallback, std::function<void(size_t bytesReceived)> progressCallback = nullptr, int timeoutMS = -1);
	// NOTE: set bDisableServiceAuth for endpoints that must be called with something other than the session token (e.g. the refresh endpoint, which needs the refresh token), otherwise the session token overwrites any Authorization header passed in inHeaders
	void SendPOSTRequest(const char* szURI, EIPProtocolVersion protover, std::map<std::string, std::string>& inHeaders, const char* szPostData, std::function<void(bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)> completionCallback, std::function<void(size_t bytesReceived)> progressCallback = nullptr, int timeoutMS = -1, bool bDisableServiceAuth = false);
	void SendPUTRequest(const char* szURI, EIPProtocolVersion protover, std::map<std::string, std::string>& inHeaders, const char* szData, std::function<void(bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)> completionCallback, std::function<void(size_t bytesReceived)> progressCallback = nullptr, int timeoutMS = -1);
	void SendS3PUTRequest(const char* szURI, EIPProtocolVersion protover, std::map<std::string, std::string>& inHeaders, std::vector<uint8_t> vecBuffer, std::function<void(bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)> completionCallback, std::function<void(size_t bytesReceived)> progressCallback = nullptr, int timeoutMS = -1);
	void SendDELETERequest(const char* szURI, EIPProtocolVersion protover, std::map<std::string, std::string>& inHeaders, const char* szData, std::function<void(bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)> completionCallback, std::function<void(size_t bytesReceived)> progressCallback = nullptr, int timeoutMS = -1);

	void Shutdown();

	bool IsProxyEnabled() const { return m_bProxyEnabled; }

	bool DeterminePlatformProxySettings();
	std::string& GetProxyAddress() { return m_strProxyAddr; }
	uint16_t GetProxyPort() const { return m_proxyPort; }

private:
	HTTPRequest* PlatformCreateRequest(EHTTPVerb htpVerb, EIPProtocolVersion protover, const char* szURI, std::map<std::string, std::string>& inHeaders, std::function<void(bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)> completionCallback,
		std::function<void(size_t bytesReceived)> progressCallback = nullptr, int timeoutMS = -1) noexcept;

private:
	// Shared WinHTTP session handle; documented thread-safe for concurrent
	// use by the per-request worker threads spawned from HTTPRequest.
	HINTERNET m_hSession = nullptr;

	bool m_bProxyEnabled = false;
	std::string m_strProxyAddr;
	uint16_t m_proxyPort;

	std::atomic<bool> m_bShuttingDown = false;

	std::vector<HTTPRequest*> m_vecRequestsPendingStart = std::vector<HTTPRequest*>();
	std::vector<HTTPRequest*> m_vecRequestsInFlight = std::vector<HTTPRequest*>();
};
