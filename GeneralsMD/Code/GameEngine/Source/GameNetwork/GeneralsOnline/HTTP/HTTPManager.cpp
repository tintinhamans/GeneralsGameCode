#include "GameNetwork/GeneralsOnline/HTTP/HTTPManager.h"
#include "../NGMP_include.h"
#include "../OnlineServices_Init.h"

HTTPManager::HTTPManager() noexcept
{

}

void HTTPManager::SendGETRequest(const char* szURI, EIPProtocolVersion protover, std::map<std::string, std::string>& inHeaders, std::function<void(bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)> completionCallback, std::function<void(size_t bytesReceived)> progressCallback, int timeoutMS)
{
	CHECK_MAIN_THREAD;

	HTTPRequest* pRequest = PlatformCreateRequest(EHTTPVerb::HTTP_VERB_GET, protover, szURI, inHeaders, completionCallback, progressCallback, timeoutMS);

	m_vecRequestsPendingStart.push_back(pRequest);
}

void HTTPManager::SendPOSTRequest(const char* szURI, EIPProtocolVersion protover, std::map<std::string, std::string>& inHeaders, const char* szPostData, std::function<void(bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)> completionCallback, std::function<void(size_t bytesReceived)> progressCallback, int timeoutMS, bool bDisableServiceAuth)
{
	CHECK_MAIN_THREAD;

	HTTPRequest* pRequest = PlatformCreateRequest(EHTTPVerb::HTTP_VERB_POST, protover, szURI, inHeaders, completionCallback, progressCallback, timeoutMS);

	if (bDisableServiceAuth)
	{
		pRequest->DisableServiceAuth();
	}

	pRequest->SetPostData(szPostData);

	m_vecRequestsPendingStart.push_back(pRequest);
}

void HTTPManager::SendPUTRequest(const char* szURI, EIPProtocolVersion protover, std::map<std::string, std::string>& inHeaders, const char* szData, std::function<void(bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)> completionCallback, std::function<void(size_t bytesReceived)> progressCallback /*= nullptr*/, int timeoutMS)
{
	CHECK_MAIN_THREAD;

	HTTPRequest* pRequest = PlatformCreateRequest(EHTTPVerb::HTTP_VERB_PUT, protover, szURI, inHeaders, completionCallback, progressCallback, timeoutMS);
	pRequest->SetPostData(szData);

	m_vecRequestsPendingStart.push_back(pRequest);
}


void HTTPManager::SendS3PUTRequest(const char* szURI, EIPProtocolVersion protover, std::map<std::string, std::string>& inHeaders, std::vector<uint8_t> vecBuffer, std::function<void(bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)> completionCallback, std::function<void(size_t bytesReceived)> progressCallback /*= nullptr*/, int timeoutMS /*= -1*/)
{
    CHECK_MAIN_THREAD;

    HTTPRequest* pRequest = PlatformCreateRequest(EHTTPVerb::HTTP_VERB_PUT, protover, szURI, inHeaders, completionCallback, progressCallback, timeoutMS);
	pRequest->DisableServiceAuth();
    pRequest->SetPostDataBuffer(vecBuffer);

    m_vecRequestsPendingStart.push_back(pRequest);
}

void HTTPManager::SendDELETERequest(const char* szURI, EIPProtocolVersion protover, std::map<std::string, std::string>& inHeaders, const char* szData, std::function<void(bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)> completionCallback, std::function<void(size_t bytesReceived)> progressCallback /*= nullptr*/, int timeoutMS)
{
	CHECK_MAIN_THREAD;

	HTTPRequest* pRequest = PlatformCreateRequest(EHTTPVerb::HTTP_VERB_DELETE, protover, szURI, inHeaders, completionCallback, progressCallback, timeoutMS);
	pRequest->SetPostData(szData);

	m_vecRequestsPendingStart.push_back(pRequest);
}

void HTTPManager::Shutdown()
{
	CHECK_MAIN_THREAD;

	// Signal that we're shutting down
	m_bShuttingDown = true;

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[HTTPManager] Shutdown initiated, canceling pending requests...");

	// Cancel all pending requests
	for (HTTPRequest* pRequest : m_vecRequestsPendingStart)
	{
		if (pRequest != nullptr)
		{
			delete pRequest;
		}
	}
	m_vecRequestsPendingStart.clear();

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[HTTPManager] Waiting for %d in-flight requests to complete...", (int)m_vecRequestsInFlight.size());

	// Wait for all in-flight requests' worker threads to finish. Each
	// HTTPRequest's destructor joins its own thread, so deleting is enough --
	// callbacks are intentionally NOT invoked here (the objects they capture
	// may already be destroyed during shutdown), matching the previous
	// curl-based behavior.
	for (HTTPRequest* pRequest : m_vecRequestsInFlight)
	{
		if (pRequest != nullptr)
		{
			delete pRequest;
		}
	}
	m_vecRequestsInFlight.clear();

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[HTTPManager] All in-flight requests completed");

	if (m_hSession != nullptr)
	{
		WinHttpCloseHandle(m_hSession);
		m_hSession = nullptr;
	}

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[HTTPManager] Shutdown complete");
}


bool HTTPManager::DeterminePlatformProxySettings()
{
	CHECK_MAIN_THREAD;

	WINHTTP_CURRENT_USER_IE_PROXY_CONFIG pProxyConfig;
	WinHttpGetIEProxyConfigForCurrentUser(&pProxyConfig);

	if (pProxyConfig.lpszProxy != nullptr)
	{
		LPWSTR ws = pProxyConfig.lpszProxy;
		std::string strFullProxy;
		strFullProxy.reserve(wcslen(ws));
		for (; *ws; ws++)
			strFullProxy += (char)*ws;

		size_t ipStart = strFullProxy.find("=");
		ipStart = (ipStart != std::string::npos) ? ipStart + 1 : 0;
		size_t ipEnd = strFullProxy.find(":", ipStart);
		if (ipEnd == std::string::npos) ipEnd = strFullProxy.size();

		m_strProxyAddr = strFullProxy.substr(ipStart, ipEnd - ipStart);

		size_t portStart = ipEnd + 1;
		size_t portEnd = strFullProxy.find(";", portStart);
		std::string strPort = strFullProxy.substr(portStart, portEnd != std::string::npos ? portEnd - portStart : std::string::npos);

		m_proxyPort = (uint16_t)atoi(strPort.c_str());
	}

	m_bProxyEnabled = pProxyConfig.lpszProxy != nullptr;

	if (pProxyConfig.lpszProxy) GlobalFree(pProxyConfig.lpszProxy);
	if (pProxyConfig.lpszAutoConfigUrl) GlobalFree(pProxyConfig.lpszAutoConfigUrl);
	if (pProxyConfig.lpszProxyBypass) GlobalFree(pProxyConfig.lpszProxyBypass);

	return m_bProxyEnabled;
}

HTTPRequest* HTTPManager::PlatformCreateRequest(EHTTPVerb httpVerb, EIPProtocolVersion protover, const char* szURI, std::map<std::string, std::string>& inHeaders, std::function<void(bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)> completionCallback, std::function<void(size_t bytesReceived)> progressCallback /*= nullptr*/, int timeoutMS /* = -1 */) noexcept
{
	CHECK_MAIN_THREAD;

	HTTPRequest* pNewRequest = new HTTPRequest(httpVerb, protover, szURI, inHeaders, completionCallback, progressCallback, timeoutMS);
	return pNewRequest;
}

HTTPManager::~HTTPManager()
{
	CHECK_MAIN_THREAD;

	Shutdown();
}

void HTTPManager::Initialize()
{
	CHECK_MAIN_THREAD;

	// Synchronous session: each HTTPRequest performs its own blocking WinHTTP
	// calls on a dedicated worker thread rather than using WinHTTP's async
	// callback model. WinHTTP session handles are documented thread-safe for
	// concurrent use, so one shared session across all worker threads is fine.
	m_hSession = WinHttpOpen(L"GeneralsOnline Client",
		WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS,
		0);

	m_bProxyEnabled = DeterminePlatformProxySettings();
}

void HTTPManager::Tick()
{
	CHECK_MAIN_THREAD;

	// start anything needing starting
	for (HTTPRequest* pRequest : m_vecRequestsPendingStart)
	{
		pRequest->StartRequest();
		m_vecRequestsInFlight.push_back(pRequest);
	}
	m_vecRequestsPendingStart.clear();

	// poll for anything that finished on its worker thread
	std::vector<HTTPRequest*> vecItemsToRemove;
	for (HTTPRequest* pRequest : m_vecRequestsInFlight)
	{
		if (pRequest != nullptr && pRequest->IsWorkerDone())
		{
			pRequest->Threaded_SetComplete();
			vecItemsToRemove.push_back(pRequest);
		}
	}

	// remove any completed
	for (HTTPRequest* pRequestToDestroy : vecItemsToRemove)
	{
		m_vecRequestsInFlight.erase(std::remove(m_vecRequestsInFlight.begin(), m_vecRequestsInFlight.end(), pRequestToDestroy));
		delete pRequestToDestroy;
	}
}
