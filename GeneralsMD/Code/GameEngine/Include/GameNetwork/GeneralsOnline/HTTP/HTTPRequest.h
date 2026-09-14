#pragma once

#include <winhttp.h>
#include <map>
#include <string>
#include <functional>
#include <thread>
#include <atomic>

enum class EHTTPVerb;
enum class EIPProtocolVersion;

class HTTPRequest
{
public:
	HTTPRequest(EHTTPVerb httpVerb, EIPProtocolVersion protover, const char* szURI, std::map<std::string, std::string>& inHeaders, std::function<void(bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)> completionCallback, std::function<void(size_t bytesReceived)>
		progressCallback = nullptr, int timeout = -1) noexcept;
	~HTTPRequest();

	// True once the worker thread has finished (successfully or not) and its
	// thread object is safe to join. Polled by HTTPManager::Tick() in place
	// of curl's curl_multi_info_read().
	bool IsWorkerDone() const { return m_bWorkerDone.load(); }

	void SetPostData(const char* szPostData);
	void SetPostDataBuffer(std::vector<uint8_t> vecBuffer);
	void StartRequest();

	void DisableServiceAuth()
	{
		m_bAppendAuthIfPresent = false;
	}

	void OnResponsePartialWrite(const std::uint8_t* pBuffer, size_t numBytes);

	bool HasStarted() const { return m_bIsStarted; }
	bool IsComplete() const { return m_bIsComplete; }

	bool NeedsProgressUpdate() const { return m_bNeedsProgressUpdate; }
	void InvokeProgressUpdateCallback()
	{
		if (m_progressCallback != nullptr)
		{
			m_progressCallback(m_currentBufSize_Used);
		}
	}

	void InvokeCallbackIfComplete();

	// Called on the main thread once IsWorkerDone() is true; joins the worker
	// thread and finalizes the request (mirrors the old Threaded_SetComplete).
	void Threaded_SetComplete();

	// mainly used for downloads
	std::vector<uint8_t> GetBuffer() { return m_vecBuffer; }
	size_t GetBufferSize() { return m_vecBuffer.size(); }

	std::string GetURI() { return m_strURI; }

private:
	// Runs on the worker thread started by StartRequest(); does the full
	// blocking WinHTTP connect/send/receive/read sequence, including the
	// combined DPI-fallback retry (see GeneralsOnline_Settings.h).
	void WorkerThreadMain();

	// One full blocking WinHTTP attempt against `uri` with the given HTTP
	// protocol flags. Returns false only on a connection-level failure
	// (DNS/TCP/TLS) -- a non-2xx HTTP status is still "success" here.
	bool WorkerThreadMain_AttemptRequest(const std::string& uri, DWORD httpProtocolFlags);

private:
	HINTERNET m_hConnect = nullptr;
	HINTERNET m_hRequest = nullptr;

	// Result of the worker thread's attempt, read on the main thread only
	// after m_bWorkerDone is observed true (happens-before via the atomic).
	bool m_bWorkerSucceeded = false;
	DWORD m_dwWinHttpError = 0;

	std::atomic<bool> m_bWorkerDone = false;
	std::thread m_workerThread;

	int m_responseCode = -1;

	EHTTPVerb m_httpVerb;

	bool m_bAppendAuthIfPresent = true;

	EIPProtocolVersion m_protover;

	int m_timeoutMS = 5000;

	std::string m_strURI;
	std::string m_strPostData;
	std::vector<uint8_t> m_vecPostDataBuffer;

	std::map<std::string, std::string> m_mapHeaders;

	std::vector<uint8_t> m_vecBuffer;
	size_t m_currentBufSize_Used = 0;

	const size_t g_initialBufSize = (1024 * 32); // 32KB

	bool m_bNeedsProgressUpdate = false;
	bool m_bIsStarted = false;
	bool m_bIsComplete = false;

	std::function<void(bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)> m_completionCallback = nullptr;

	std::function<void(size_t bytesReceived)> m_progressCallback = nullptr;
};
