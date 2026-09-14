#include "GameNetwork/GeneralsOnline/HTTP/HTTPRequest.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_Init.h"
#include "GameNetwork/GeneralsOnline/HTTP/HTTPManager.h"
#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"

#include <winhttp.h>
#include <algorithm>

namespace
{
	std::wstring Utf8ToWide(const std::string& str)
	{
		if (str.empty())
			return std::wstring();

		int wideLen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
		std::wstring wide(wideLen, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), wide.data(), wideLen);
		return wide;
	}

	const wchar_t* VerbToWide(EHTTPVerb verb)
	{
		switch (verb)
		{
			case EHTTPVerb::HTTP_VERB_GET:    return L"GET";
			case EHTTPVerb::HTTP_VERB_POST:   return L"POST";
			case EHTTPVerb::HTTP_VERB_PUT:    return L"PUT";
			case EHTTPVerb::HTTP_VERB_DELETE: return L"DELETE";
		}
		return L"GET";
	}

	// Maps the abstract, platform-agnostic EHTTPVersion setting to WinHTTP's
	// WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL flags. Lives here (not in
	// GeneralsOnline_Settings) since it's a WinHTTP-specific translation, not
	// something the settings class should know about.
	DWORD HTTPVersionToWinHttpFlags(EHTTPVersion version)
	{
		switch (version)
		{
			case EHTTPVersion::HTTP_VERSION_1_1: return 0; // HTTP/2 disabled -> falls back to 1.1
			case EHTTPVersion::HTTP_VERSION_2_0: return WINHTTP_PROTOCOL_FLAG_HTTP2;
			case EHTTPVersion::HTTP_VERSION_AUTO:
			default:                             return WINHTTP_PROTOCOL_FLAG_HTTP2; // WinHTTP's normal default (auto-negotiated)
		}
	}

	// Fork-specific: swaps the default GeneralsOnline API host for the
	// regional alternative (see GetAPIEndpoint() in OnlineServices_Init.cpp).
	// Only meaningful for requests actually targeting that host - a no-op for
	// anything else (e.g. third-party S3 upload URLs).
	const char* const g_szDefaultAPIHost = "api.playgenerals.online";
	const char* const g_szAlternativeAPIHost = "api-ru.playgenerals.online";

	bool TrySubstituteAlternativeHost(std::string& uri)
	{
		size_t pos = uri.find(g_szDefaultAPIHost);
		if (pos == std::string::npos)
			return false;

		uri.replace(pos, strlen(g_szDefaultAPIHost), g_szAlternativeAPIHost);
		return true;
	}
}

HTTPRequest::HTTPRequest(EHTTPVerb httpVerb, EIPProtocolVersion protover, const char* szURI, std::map<std::string, std::string>& inHeaders, std::function<void(bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)> completionCallback,
	std::function<void(size_t bytesReceived)> progressCallback /*= nullptr*/, int timeoutMS/*= -1*/) noexcept
{
	// -1 means use default
	if (timeoutMS > 0)
	{
		m_timeoutMS = timeoutMS;
	}

	m_httpVerb = httpVerb;
	m_protover = protover;
	m_strURI = szURI;
	m_completionCallback = completionCallback;

	m_mapHeaders = inHeaders;

	m_progressCallback = progressCallback;
}

HTTPRequest::~HTTPRequest()
{
	if (m_workerThread.joinable())
	{
		m_workerThread.join();
	}

	if (m_hRequest != nullptr)
	{
		WinHttpCloseHandle(m_hRequest);
		m_hRequest = nullptr;
	}

	if (m_hConnect != nullptr)
	{
		WinHttpCloseHandle(m_hConnect);
		m_hConnect = nullptr;
	}

	m_vecBuffer.clear();
}


void HTTPRequest::SetPostData(const char* szPostData)
{
	// TODO_HTTP: Error if verb isnt post
	m_strPostData = std::string(szPostData);

	NetworkLog(ELogVerbosity::LOG_DEBUG, "[%p|%s|Verb %d] Transfer is created: Body is %s", this, m_strURI.c_str(), m_httpVerb, szPostData);
}

void HTTPRequest::SetPostDataBuffer(std::vector<uint8_t> vecBuffer)
{
	m_vecPostDataBuffer = std::move(vecBuffer);
}

void HTTPRequest::StartRequest()
{
	m_bIsStarted = true;
	m_bIsComplete = false;

	m_vecBuffer.resize(g_initialBufSize);

	m_currentBufSize_Used = 0;

	NetworkLog(ELogVerbosity::LOG_DEBUG, "[%p|%s|Verb %d] Transfer is starting: Body is %s", this, m_strURI.c_str(), m_httpVerb, m_strPostData.c_str());

	m_workerThread = std::thread(&HTTPRequest::WorkerThreadMain, this);
}

void HTTPRequest::OnResponsePartialWrite(const std::uint8_t* pBuffer, size_t numBytes)
{
	if (m_currentBufSize_Used + numBytes > m_vecBuffer.size())
	{
		size_t newSize = std::max<size_t>(m_vecBuffer.size() * 2, m_currentBufSize_Used + numBytes);
		m_vecBuffer.resize(newSize);
	}

	std::copy(pBuffer, pBuffer + numBytes, m_vecBuffer.begin() + m_currentBufSize_Used);
	m_currentBufSize_Used += numBytes;

	NetworkLog(ELogVerbosity::LOG_DEBUG, "[%p] Received: %d bytes", this, numBytes);

	InvokeProgressUpdateCallback();
}

void HTTPRequest::InvokeCallbackIfComplete()
{
	if (m_bIsComplete)
	{
		if (m_completionCallback != nullptr)
		{
			// Convert m_vecBuffer to std::string for m_strResponse
			std::string strResponse;
			if (!m_vecBuffer.empty() && m_currentBufSize_Used > 0)
			{
				strResponse = std::string(reinterpret_cast<const char*>(m_vecBuffer.data()), m_currentBufSize_Used);
			}
			else
			{
				strResponse.clear();
			}
			m_completionCallback(m_bWorkerSucceeded, m_responseCode, strResponse, this);
		}
	}
}

void HTTPRequest::Threaded_SetComplete()
{
	if (m_workerThread.joinable())
	{
		m_workerThread.join();
	}

	m_bIsComplete = true;

	// finalize the size, so we can use .size etc
	m_vecBuffer.resize(m_currentBufSize_Used);

	std::string strURIRedacted = m_strURI;

#if !_DEBUG
	size_t tokenpos = strURIRedacted.find("token:");
	if (tokenpos != -1)
	{
		std::string strReplace = "<redacted>";
		const size_t tokenLen = 32;
		strURIRedacted = strURIRedacted.replace(tokenpos + 6, tokenLen, strReplace);
	}
#endif

	std::string strResponse = std::string(reinterpret_cast<const char*>(m_vecBuffer.data()), m_currentBufSize_Used);
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[%p|%s|Verb %d] Transfer is complete: %d bytes total! Success is %d (WinHTTP error %lu)", this, strURIRedacted.c_str(), m_httpVerb, m_currentBufSize_Used, m_bWorkerSucceeded ? 1 : 0, m_dwWinHttpError);

#if !_DEBUG
	static const std::string strSeedKey = "\"RNGSeed\":";
	for (size_t seedPos = strResponse.find(strSeedKey); seedPos != std::string::npos; seedPos = strResponse.find(strSeedKey, seedPos))
	{
		size_t valueStart = seedPos + strSeedKey.length();
		size_t valueEnd = strResponse.find_first_of(",}", valueStart);
		if (valueEnd == std::string::npos)
			break;

		strResponse.replace(valueStart, valueEnd - valueStart, "<redacted>");
		seedPos = valueStart;
	}

	std::transform(strResponse.begin(), strResponse.end(), strResponse.begin(),
		[](unsigned char c) { return std::tolower(c); });
	if (strResponse.find("token") != std::string::npos)
	{
		strResponse = "<redacted>";
	}
#endif

	NetworkLog(ELogVerbosity::LOG_RELEASE, "[%p|%s|Verb %d] Response was %d - %s!", this, strURIRedacted.c_str(), m_httpVerb, m_responseCode, strResponse.c_str());

	// trigger callback
	InvokeCallbackIfComplete();
}

// Performs one full blocking WinHTTP connect/send/receive/read attempt
// against `uri`, with `httpProtocolFlags` controlling HTTP/1.1 vs HTTP/2.
// Returns true on success (2xx-or-not, just "we got a response"); false on
// a connection-level failure (DNS/TCP/TLS failure, no response at all).
bool HTTPRequest::WorkerThreadMain_AttemptRequest(const std::string& uri, DWORD httpProtocolFlags)
{
	HINTERNET hSession = NGMP_OnlineServicesManager::GetInstance()->GetHTTPManager()->GetSessionHandle();

	URL_COMPONENTSW urlComponents = {};
	urlComponents.dwStructSize = sizeof(urlComponents);

	wchar_t szHost[256] = {};
	wchar_t szPath[2048] = {};
	urlComponents.lpszHostName = szHost;
	urlComponents.dwHostNameLength = ARRAYSIZE(szHost);
	urlComponents.lpszUrlPath = szPath;
	urlComponents.dwUrlPathLength = ARRAYSIZE(szPath);
	urlComponents.dwSchemeLength = (DWORD)-1;

	std::wstring wideURI = Utf8ToWide(uri);
	// LPURL_COMPONENTS resolves to the ANSI URL_COMPONENTSA alias in this TU
	// (wininet.h's UNICODE-gated typedef wins the "URL_COMPONENTS" name over
	// winhttp.h's own, since this project isn't built with UNICODE defined).
	// WinHttpCrackUrl's real ABI is always wide, so the cast is safe -- the
	// struct we're passing (URL_COMPONENTSW) is the one it actually expects.
	if (!WinHttpCrackUrl(wideURI.c_str(), (DWORD)wideURI.length(), 0, reinterpret_cast<LPURL_COMPONENTS>(&urlComponents)))
	{
		m_dwWinHttpError = GetLastError();
		return false;
	}

	bool bIsHttps = (urlComponents.nScheme == INTERNET_SCHEME_HTTPS);

	if (m_hRequest != nullptr) { WinHttpCloseHandle(m_hRequest); m_hRequest = nullptr; }
	if (m_hConnect != nullptr) { WinHttpCloseHandle(m_hConnect); m_hConnect = nullptr; }

	m_hConnect = WinHttpConnect(hSession, urlComponents.lpszHostName, urlComponents.nPort, 0);
	if (m_hConnect == nullptr)
	{
		m_dwWinHttpError = GetLastError();
		return false;
	}

	DWORD dwRequestFlags = bIsHttps ? WINHTTP_FLAG_SECURE : 0;
	m_hRequest = WinHttpOpenRequest(m_hConnect, VerbToWide(m_httpVerb), urlComponents.lpszUrlPath, nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, dwRequestFlags);
	if (m_hRequest == nullptr)
	{
		m_dwWinHttpError = GetLastError();
		return false;
	}

	WinHttpSetOption(m_hRequest, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &httpProtocolFlags, sizeof(httpProtocolFlags));

	WinHttpSetTimeouts(m_hRequest, m_timeoutMS, m_timeoutMS, m_timeoutMS, m_timeoutMS);

#if _DEBUG
	HTTPManager* pHTTPManager = NGMP_OnlineServicesManager::GetInstance()->GetHTTPManager();
	if (pHTTPManager->IsProxyEnabled())
	{
		WINHTTP_PROXY_INFO proxyInfo = {};
		std::wstring wideProxy = Utf8ToWide(pHTTPManager->GetProxyAddress() + ":" + std::to_string(pHTTPManager->GetProxyPort()));
		proxyInfo.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
		proxyInfo.lpszProxy = wideProxy.data();
		WinHttpSetOption(m_hRequest, WINHTTP_OPTION_PROXY, &proxyInfo, sizeof(proxyInfo));
	}

	DWORD dwSecurityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
		| SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
	WinHttpSetOption(m_hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwSecurityFlags, sizeof(dwSecurityFlags));
#endif
	// NOTE_NGMP: WinHTTP always validates certificates via the OS certificate
	// store (SChannel underneath) in release, so there is no cacert.pem file
	// or "CA store is bad" bypass to maintain here anymore.

	// Are we authenticated? attach our auth header
	NGMP_OnlineServices_AuthInterface* pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	if (pAuthInterface != nullptr && pAuthInterface->IsLoggedIn())
	{
		if (m_bAppendAuthIfPresent)
		{
			m_mapHeaders["Authorization"] = "Bearer " + pAuthInterface->GetAuthToken();
		}
	}

	std::string strHeaders;
	for (auto& kvPair : m_mapHeaders)
	{
		strHeaders += kvPair.first + ": " + kvPair.second + "\r\n";
	}
	std::wstring wideHeaders = Utf8ToWide(strHeaders);

	const void* pPostData = nullptr;
	DWORD dwPostDataLen = 0;
	if (m_httpVerb == EHTTPVerb::HTTP_VERB_POST || m_httpVerb == EHTTPVerb::HTTP_VERB_PUT || m_httpVerb == EHTTPVerb::HTTP_VERB_DELETE)
	{
		if (!m_vecPostDataBuffer.empty())
		{
			pPostData = m_vecPostDataBuffer.data();
			dwPostDataLen = (DWORD)m_vecPostDataBuffer.size();
		}
		else if (!m_strPostData.empty())
		{
			pPostData = m_strPostData.data();
			dwPostDataLen = (DWORD)m_strPostData.size();
		}
	}

	BOOL bSendResult = WinHttpSendRequest(m_hRequest,
		wideHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wideHeaders.c_str(),
		wideHeaders.empty() ? 0 : (DWORD)wideHeaders.length(),
		const_cast<void*>(pPostData), dwPostDataLen, dwPostDataLen, 0);
	if (!bSendResult)
	{
		m_dwWinHttpError = GetLastError();
		return false;
	}

	if (!WinHttpReceiveResponse(m_hRequest, nullptr))
	{
		m_dwWinHttpError = GetLastError();
		return false;
	}

	DWORD dwStatusCode = 0;
	DWORD dwStatusCodeSize = sizeof(dwStatusCode);
	WinHttpQueryHeaders(m_hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwStatusCodeSize, WINHTTP_NO_HEADER_INDEX);
	m_responseCode = (int)dwStatusCode;

	// read the body
	for (;;)
	{
		DWORD dwAvailable = 0;
		if (!WinHttpQueryDataAvailable(m_hRequest, &dwAvailable))
		{
			m_dwWinHttpError = GetLastError();
			return false;
		}

		if (dwAvailable == 0)
			break;

		std::vector<uint8_t> readBuf(dwAvailable);
		DWORD dwRead = 0;
		if (!WinHttpReadData(m_hRequest, readBuf.data(), dwAvailable, &dwRead))
		{
			m_dwWinHttpError = GetLastError();
			return false;
		}

		if (dwRead == 0)
			break;

		OnResponsePartialWrite(readBuf.data(), dwRead);
	}

	return true;
}

void HTTPRequest::WorkerThreadMain()
{
	EHTTPVersion httpVersionSetting = NGMP_OnlineServicesManager::Settings.Network_GetHTTPVersion();
	ENetworkEndpoint endpointSetting = NGMP_OnlineServicesManager::Settings.Network_UseAlternativeEndpoint();

	std::string strFirstAttemptURI = m_strURI;
	if (endpointSetting == ENetworkEndpoint::NETWORK_ENDPOINT_ALTERNATIVE)
	{
		TrySubstituteAlternativeHost(strFirstAttemptURI);
	}
	// NETWORK_ENDPOINT_DEFAULT and NETWORK_ENDPOINT_AUTO both start on whatever
	// host GetAPIEndpoint() already built m_strURI against (the default host).

	DWORD dwFirstProtocolFlags = HTTPVersionToWinHttpFlags(httpVersionSetting);

	bool bSuccess = WorkerThreadMain_AttemptRequest(strFirstAttemptURI, dwFirstProtocolFlags);

	// Combined DPI/censorship fallback: only when BOTH settings are Auto, and
	// only on a connection-level failure (m_responseCode still unset means we
	// never even got a response to be a non-2xx status for).
	bool bBothAuto = (httpVersionSetting == EHTTPVersion::HTTP_VERSION_AUTO) && (endpointSetting == ENetworkEndpoint::NETWORK_ENDPOINT_AUTO);
	if (!bSuccess && bBothAuto)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[%p|%s] Initial request failed (WinHTTP error %lu); retrying with alternative host + HTTP/1.1", this, m_strURI.c_str());

		std::string strFallbackURI = m_strURI;
		TrySubstituteAlternativeHost(strFallbackURI);

		m_strURI = strFallbackURI;
		m_currentBufSize_Used = 0;

		bSuccess = WorkerThreadMain_AttemptRequest(strFallbackURI, HTTPVersionToWinHttpFlags(EHTTPVersion::HTTP_VERSION_1_1));
	}

	m_bWorkerSucceeded = bSuccess;
	m_bWorkerDone.store(true);
}
