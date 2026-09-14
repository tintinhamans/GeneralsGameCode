#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"
#include "GameNetwork/GeneralsOnline/NGMP_include.h"
#include "GameNetwork/GeneralsOnline/NetworkPacket.h"
#include "GameNetwork/GeneralsOnline/NetworkBitstream.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_Moderation.h"
#include "GameNetwork/GeneralsOnline/json.hpp"
#include "../OnlineServices_Init.h"
#include "../HTTP/HTTPManager.h"
#include "GameNetwork/GameSpy/PeerDefs.h"

// -----------------------------
// Module info structure
// -----------------------------
struct GOModuleInfo {
    std::string path;
    std::string directory;
    DWORD size;
    void* baseAddress;
    std::string publisher;
    bool isSigned;
};

#include <windows.h>
#include <psapi.h>
#include <wincrypt.h>
#include <softpub.h>
#include <string>
#include <vector>
#include <iostream>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "wintrust.lib")

// -----------------------------
// UTF-8 <-> UTF-16 helpers
// -----------------------------
std::wstring ToWide(const std::string& s) {
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], size);
    out.resize(size - 1);
    return out;
}

std::string ToUtf8(const std::wstring& s) {
    int size = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &out[0], size, nullptr, nullptr);
    out.resize(size - 1);
    return out;
}

std::vector<GOModuleInfo> GetLoadedModules() {
    std::vector<GOModuleInfo> modules;

    HMODULE hMods[1024];
    DWORD cbNeeded;

    HANDLE hProcess = GetCurrentProcess();

    if (!EnumProcessModulesEx(hProcess, hMods, sizeof(hMods), &cbNeeded, LIST_MODULES_ALL))
        return modules;

    int count = cbNeeded / sizeof(HMODULE);

    for (int i = 0; i < count; i++) {
        wchar_t wpath[MAX_PATH];
        if (!GetModuleFileNameExW(hProcess, hMods[i], wpath, MAX_PATH))
            continue;

        MODULEINFO modInfo;
        if (!GetModuleInformation(hProcess, hMods[i], &modInfo, sizeof(modInfo)))
            continue;

        std::string path = ToUtf8(wpath);

        std::string directory;
        size_t pos = path.find_last_of("/\\");
        if (pos != std::string::npos)
            directory = path.substr(0, pos);
        else
            directory = "";

        bool isSigned = false;
        //std::string publisher = GetPublisherFromSignature(path, isSigned);
		std::string publisher = "";


        modules.push_back({
            path,
            directory,
            (DWORD)modInfo.SizeOfImage,
            modInfo.lpBaseOfDll,
            publisher,
            isSigned
            });
    }

    return modules;
}


namespace
{
	std::wstring WSUtf8ToWide(const std::string& str)
	{
		if (str.empty())
			return std::wstring();

		int wideLen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
		std::wstring wide(wideLen, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), wide.data(), wideLen);
		return wide;
	}
}

WebSocket::WebSocket()
{
}

WebSocket::~WebSocket()
{
	Shutdown();
	// Only call Shutdown if it has not been initiated already.
	// NGMP_OnlineServicesManager::Shutdown() calls Shutdown() before releasing the shared_ptr,
	// so calling it again from the destructor would redundantly block for another 100ms sleep
	// and attempt to free already-released WinHTTP resources.
	if (!m_bShuttingDown)
	{
		Shutdown();
	}
}

int WebSocket::Ping()
{
	// WinHTTP has no application-facing way to send a raw WS ping frame
	// (WinHttpWebSocketSend's buffer-type enum only covers UTF8/binary
	// message/fragment and close); it manages ping/pong internally at the
	// protocol level. Liveness is tracked purely via the JSON-level PING
	// below, same as the pong side already only looks at the JSON PONG.
	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::PING;
	std::string strBody = j.dump();

	Send(strBody.c_str());

	return 0;
}

void WebSocket::ConnectThreadMain(std::string strURL, bool bIsReconnect)
{
	if (m_hWebSocket != nullptr) { WinHttpCloseHandle(m_hWebSocket); m_hWebSocket = nullptr; }
	if (m_hConnect != nullptr) { WinHttpCloseHandle(m_hConnect); m_hConnect = nullptr; }

	// WinHTTP doesn't parse ws(s):// as a URL scheme -- the WebSocket upgrade
	// is performed as a normal http(s) request that's then upgraded in place.
	std::string strHttpURL = strURL;
	if (strHttpURL.rfind("wss://", 0) == 0)
		strHttpURL = "https://" + strHttpURL.substr(6);
	else if (strHttpURL.rfind("ws://", 0) == 0)
		strHttpURL = "http://" + strHttpURL.substr(5);

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

	std::wstring wideURL = WSUtf8ToWide(strHttpURL);
	// See the comment at HTTPRequest.cpp's WinHttpCrackUrl call -- LPURL_COMPONENTS
	// is aliased to the ANSI struct in this non-UNICODE TU, but the real ABI is wide.
	if (!WinHttpCrackUrl(wideURL.c_str(), (DWORD)wideURL.length(), 0, reinterpret_cast<LPURL_COMPONENTS>(&urlComponents)))
	{
		m_dwConnectWinHttpError = GetLastError();
		m_bConnectSucceeded = false;
		m_connectHttpStatus = -1;
		m_bConnectAttemptDone.store(true);
		return;
	}

	bool bIsSecure = (urlComponents.nScheme == INTERNET_SCHEME_HTTPS);

	m_hConnect = WinHttpConnect(hSession, urlComponents.lpszHostName, urlComponents.nPort, 0);
	if (m_hConnect == nullptr)
	{
		m_dwConnectWinHttpError = GetLastError();
		m_bConnectSucceeded = false;
		m_connectHttpStatus = -1;
		m_bConnectAttemptDone.store(true);
		return;
	}

	HINTERNET hRequest = WinHttpOpenRequest(m_hConnect, L"GET", urlComponents.lpszUrlPath, nullptr,
		WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, bIsSecure ? WINHTTP_FLAG_SECURE : 0);
	if (hRequest == nullptr)
	{
		m_dwConnectWinHttpError = GetLastError();
		m_bConnectSucceeded = false;
		m_connectHttpStatus = -1;
		m_bConnectAttemptDone.store(true);
		return;
	}

#if _DEBUG
	DWORD dwSecurityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
		| SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
	WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwSecurityFlags, sizeof(dwSecurityFlags));
#endif
	// NOTE_NGMP: WinHTTP always validates certificates via the OS certificate
	// store (SChannel underneath) in release, so there is no cacert.pem file
	// or "CA store is bad" bypass to maintain here anymore.

	EHTTPVersion httpVersionSetting = NGMP_OnlineServicesManager::Settings.Network_GetHTTPVersion();
	DWORD dwProtocolFlags = (httpVersionSetting == EHTTPVersion::HTTP_VERSION_1_1) ? 0 : WINHTTP_PROTOCOL_FLAG_HTTP2;
	WinHttpSetOption(hRequest, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &dwProtocolFlags, sizeof(dwProtocolFlags));

	// Must be set before WinHttpSendRequest() to signal the upgrade.
	WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);

	// ws needs auth
	NGMP_OnlineServices_AuthInterface* pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	if (pAuthInterface == nullptr)
	{
		WinHttpCloseHandle(hRequest);
		m_bConnectSucceeded = false;
		m_connectHttpStatus = -1;
		m_bConnectAttemptDone.store(true);
		return;
	}

	std::string strHeaders;
	strHeaders += "Authorization: Bearer " + pAuthInterface->GetAuthToken() + "\r\n";
	strHeaders += std::string("is-reconnect: ") + (bIsReconnect ? "true" : "false") + "\r\n";
	std::wstring wideHeaders = WSUtf8ToWide(strHeaders);

	BOOL bSendResult = WinHttpSendRequest(hRequest, wideHeaders.c_str(), (DWORD)wideHeaders.length(),
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

	bool bSucceeded = false;
	int httpStatus = -1;

	if (bSendResult && WinHttpReceiveResponse(hRequest, nullptr))
	{
		DWORD dwStatusCode = 0;
		DWORD dwStatusCodeSize = sizeof(dwStatusCode);
		WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwStatusCodeSize, WINHTTP_NO_HEADER_INDEX);
		httpStatus = (int)dwStatusCode;

		m_hWebSocket = WinHttpWebSocketCompleteUpgrade(hRequest, 0);
		bSucceeded = (m_hWebSocket != nullptr);
		if (!bSucceeded)
		{
			m_dwConnectWinHttpError = GetLastError();
		}
	}
	else
	{
		m_dwConnectWinHttpError = GetLastError();
	}

	// No longer needed once the websocket handle is obtained (or the upgrade
	// failed) -- WinHttpWebSocketCompleteUpgrade doesn't consume this handle.
	WinHttpCloseHandle(hRequest);

	m_bConnectSucceeded = bSucceeded;
	m_connectHttpStatus = httpStatus;
	m_bConnectAttemptDone.store(true);
}

void WebSocket::Connect(const char* url, bool bIsReconnect, std::function<void(void)> fnWebsocketConnectedCallback)
{
	if (m_bConnected)
	{
		return;
	}

	m_lastPong = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();

	if (m_connectThread.joinable())
	{
		m_connectThread.join();
	}

	m_fnWebsocketConnectedCallback = fnWebsocketConnectedCallback;
	m_strWebsocketAddr = std::string(url);
	m_bConnectAttemptDone.store(false);

	m_connectThread = std::thread(&WebSocket::ConnectThreadMain, this, std::string(url), bIsReconnect);
}

void WebSocket::SendData_RoomChatMessage(UnicodeString& msg, bool bIsAction)
{
	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::NETWORK_ROOM_CHAT_FROM_CLIENT;
	j["message"] = to_utf8(msg.str());
	j["action"] = bIsAction;
	std::string strBody = j.dump(-1, 32, true);

	Send(strBody.c_str());
}

void WebSocket::SendData_MarkReady(bool bReady)
{
	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::NETWORK_ROOM_MARK_READY;
	j["ready"] = bReady;
	std::string strBody = j.dump();

	Send(strBody.c_str());
}


void WebSocket::SendData_JoinNetworkRoom(int roomID, uint64_t requestID)
{
	nlohmann::json j;
	j["msg_id"] = EWebSocketMessageID::NETWORK_ROOM_CHANGE_ROOM;
	j["room"] = roomID;
	if (requestID != 0)
	{
		j["request_id"] = requestID;
	}
	std::string strBody = j.dump();

	Send(strBody.c_str());
}

void WebSocket::Disconnect()
{
	if (!m_bConnected)
	{
		return;
	}

	if (m_hWebSocket != nullptr)
	{
		// send close
		WinHttpWebSocketClose(m_hWebSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
	}

	// Signal the receive thread to stop and wait for it -- WinHttpWebSocketClose()
	// unblocks any pending WinHttpWebSocketReceive() on the handle.
	m_bReceiveThreadShouldStop.store(true);
	if (m_receiveThread.joinable())
	{
		m_receiveThread.join();
	}

	if (m_hWebSocket != nullptr)
	{
		WinHttpCloseHandle(m_hWebSocket);
		m_hWebSocket = nullptr;
	}

	m_vecWSPartialBuffer.clear();
	m_bConnected = false;
}

void WebSocket::Send(const char* send_payload)
{
	if (!AcquireLock())
	{
		return;
	}

	if (!m_bConnected)
	{
		// just queue it instead
		m_vecQueuedOutboungMsgs.push_back(std::string(send_payload));

		ReleaseLock();
		return;
	}

	// WinHTTP doesn't guarantee thread-safety for concurrent sends on one
	// handle, so this stays serialized through the same lock as before.
	DWORD result = WinHttpWebSocketSend(m_hWebSocket, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
		(PVOID)send_payload, (DWORD)strlen(send_payload));

	if (result != NO_ERROR)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "WinHttpWebSocketSend() failed: %lu\n", result);
	}

	ReleaseLock();
}

class WebSocketMessageBase
{
public:
	EWebSocketMessageID msg_id;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessageBase, msg_id)
};

class WebSocketMessage_ModerationNotice : public WebSocketMessageBase
{
public:
	std::string action_type;
	std::string reason;
	std::string scope_type;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(WebSocketMessage_ModerationNotice, msg_id, action_type, reason, scope_type)
};

class WebSocketMessage_ModerationCommandResult : public WebSocketMessageBase
{
public:
	uint64_t request_id = 0;
	bool success = false;
	std::string error_code;
	std::string message;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(
		WebSocketMessage_ModerationCommandResult,
		msg_id,
		request_id,
		success,
		error_code,
		message)
};

class WebSocketMessage_NetworkStartSignalling : public WebSocketMessageBase
{
public:
	int64_t lobby_id;
	int64_t user_id;
	uint16_t preferred_port;
	std::string middleware_id;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_NetworkStartSignalling, msg_id, lobby_id, user_id, preferred_port, middleware_id)
};

class WebSocketMessage_ACRegisterPlayer : public WebSocketMessageBase
{
public:
    int64_t user_id;
    std::string mwid;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_ACRegisterPlayer, msg_id, user_id, mwid)
};

class WebSocketMessage_ACDeregisterPlayer : public WebSocketMessageBase
{
public:
    int64_t user_id;
    std::string mwid;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_ACDeregisterPlayer, msg_id, user_id, mwid)
};

class WebSocketMessage_NetworkDisconnectPlayer : public WebSocketMessageBase
{
public:
	int64_t lobby_id;
	int64_t user_id;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_NetworkDisconnectPlayer, msg_id, lobby_id, user_id)
};

class WebSocketMessage_MatchmakingAction_JoinPrearrangedLobby : public WebSocketMessageBase
{
public:
	int64_t lobby_id;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_MatchmakingAction_JoinPrearrangedLobby, msg_id, lobby_id)
};


class WebSocketMessage_RoomChatIncoming : public WebSocketMessageBase
{
public:
	std::string message;
	bool action;
	bool admin;
	bool name_change;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_RoomChatIncoming, msg_id, message, action, admin, name_change)
};

class WebSocketMessage_Social_FriendChatMessage_Incoming : public WebSocketMessageBase
{
public:
	int64_t source_user_id;
	int64_t target_user_id;
	std::string message;
	
	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_Social_FriendChatMessage_Incoming, msg_id, source_user_id, target_user_id, message)
};

class WebSocketMessage_Social_FriendStatusChanged : public WebSocketMessageBase
{
public:
	std::string display_name;
	bool online;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_Social_FriendStatusChanged, display_name, online)
};

class WebSocketMessage_Social_FriendRequestAccepted : public WebSocketMessageBase
{
public:
	std::string display_name;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_Social_FriendRequestAccepted, display_name)
};

class WebSocketMessage_FriendsOverallStatusUpdate : public WebSocketMessageBase
{
public:
	int num_online;
	int num_pending;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_FriendsOverallStatusUpdate, num_online, num_pending)
};

class WebSocketMessage_NetworkSignal : public WebSocketMessageBase
{
public:
	int64_t target_user_id = -1;
	std::vector<uint8_t> payload;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_NetworkSignal, target_user_id, payload)
};

class WebSocketMessage_AnticheatMessage : public WebSocketMessageBase
{
public:
    int64_t target_user_id = -1;
    std::vector<uint8_t> payload;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_AnticheatMessage, target_user_id, payload)
};

class WebSocketMessage_ServerProbe : public WebSocketMessageBase
{
public:
	std::string url;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_ServerProbe, msg_id, url)
};

class WebSocketMessage_StartGameResponse : public WebSocketMessageBase
{
public:
    std::string screenshot_url;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_StartGameResponse, msg_id, screenshot_url)
};

class WebSocketMessage_LobbyChatIncoming : public WebSocketMessageBase
{
public:
	std::string message;
	bool action;
	bool announcement;
	bool show_announcement_to_host;
	int64_t user_id;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_LobbyChatIncoming, msg_id, message, action, announcement, show_announcement_to_host, user_id)
};

class WebSocketMessage_MatchmakingMessage : public WebSocketMessageBase
{
public:
	std::string message;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_MatchmakingMessage, msg_id, message)
};

class WebSocketMessage_Social_NewFriendRequest : public WebSocketMessageBase
{
public:
	std::string display_name;

	NLOHMANN_DEFINE_TYPE_INTRUSIVE(WebSocketMessage_Social_NewFriendRequest, msg_id, display_name)
};

static bool JSONDeserialize(const char* szBuffer, nlohmann::json* jsonObject)
{
	try
	{
		*jsonObject = nlohmann::json::parse(szBuffer);
		return true;
	}
	catch (nlohmann::json::exception& jsonException)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "JSONDeserialize: Unparsable JSON: %s (%s)", szBuffer, jsonException.what());
		return false;
	}
	catch (...)
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "JSONDeserialize: Unparsable JSON: %s", szBuffer);
		return false;
	}

	return false;
}

template<typename T>
static bool JSONGetAsObject(nlohmann::json& jsonObject, T* outMsg)
{
	try
	{
		*outMsg = jsonObject.get<T>();

		return true;
	}
	catch (nlohmann::json::exception& jsonException)
	{
		std::string targetTypeName = typeid(T).name();
		NetworkLog(ELogVerbosity::LOG_RELEASE, "JSONGetAsObject: Unparsable JSON: Target Type is %s (%s)", targetTypeName.c_str(), jsonException.what());
		return false;
	}
	catch (...)
	{
		std::string targetTypeName = typeid(T).name();
		NetworkLog(ELogVerbosity::LOG_RELEASE, "JSONGetAsObject: Unparsable JSON: Target Type is %s", targetTypeName.c_str());
		return false;
	}

	return false;
}

void WebSocket::ReceiveThreadMain()
{
	// Reused across calls; WinHttpWebSocketReceive() tells us via dwBytesRead
	// how much of it was actually filled this call.
	std::vector<uint8_t> buffer(8196 * 4);

	while (!m_bReceiveThreadShouldStop.load())
	{
		DWORD dwBytesRead = 0;
		WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType;

		DWORD result = WinHttpWebSocketReceive(m_hWebSocket, buffer.data(), (DWORD)buffer.size(), &dwBytesRead, &bufferType);

		if (m_bReceiveThreadShouldStop.load())
		{
			// Disconnect() is tearing this down deliberately -- don't report
			// the resulting failure/close as a connection error.
			break;
		}

		if (result != NO_ERROR)
		{
			WSIncomingChunk chunk;
			chunk.bIsConnectionError = true;
			chunk.dwWinHttpError = result;

			if (AcquireLock())
			{
				m_vecIncomingMessages.push(std::move(chunk));
				ReleaseLock();
			}
			break;
		}

		if (bufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
		{
			WSIncomingChunk chunk;
			chunk.bIsExplicitClose = true;

			if (AcquireLock())
			{
				m_vecIncomingMessages.push(std::move(chunk));
				ReleaseLock();
			}
			break;
		}

		// UTF8_MESSAGE/BINARY_MESSAGE mean this call completed the logical
		// message; the _FRAGMENT_ variants mean more calls are needed for the
		// same message (mirrors curl's CURLWS_CONT/bytesleft distinction).
		WSIncomingChunk chunk;
		chunk.data.assign(buffer.begin(), buffer.begin() + dwBytesRead);
		chunk.bIsText = (bufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || bufferType == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE);
		chunk.bIsMessageComplete = (bufferType == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || bufferType == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE);

		if (AcquireLock())
		{
			m_vecIncomingMessages.push(std::move(chunk));
			ReleaseLock();
		}
	}
}

//static std::string strSignal = "str:1 ";
void WebSocket::Tick()
{
    if (!AcquireLock())
    {
        return;
    }

	// attempting to reconnect?
	if (m_bReconnecting)
	{
		int64_t currTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();

		int maxReconnectAttempts = (TheNGMPGame != nullptr && TheNGMPGame->isGameInProgress()) ? maxReconnectAttempts_Ingame : maxReconnectAttempts_Frontend;
		if (m_numReconnectAttempts >= maxReconnectAttempts)
		{
			// fully disconnect
            NetworkLog(ELogVerbosity::LOG_RELEASE, "Going to teardown (reconnect 1)");
            NGMP_OnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::LOST_CONNECTION);
            m_bConnected = false;
            m_vecWSPartialBuffer.clear();

            // clear reconnection flags
            m_bReconnecting = false;
            m_numReconnectAttempts = 0;
            m_lastReconnectAttempt = -1;
		}
		else
		{
			int timeBetweenReconnectAttempts = (TheNGMPGame != nullptr && TheNGMPGame->isGameInProgress()) ? timeBetweenReconnectAttempts_Ingame : timeBetweenReconnectAttempts_Frontend;

            if (currTime - m_lastReconnectAttempt >= timeBetweenReconnectAttempts)
            {
                m_lastReconnectAttempt = currTime;
                ++m_numReconnectAttempts;

				Connect(m_strWebsocketAddr.c_str(), true, nullptr);
            }
		}
	}



	/*
	if (strSignal.length() == 6)
	{
		for (int i = 0; i < 5000 - 6; ++i)
		{
			if (i == 5000 - 6 - 1)
			{
				strSignal += "+";
			}
			else
			{
				strSignal += i % 2 == 0 ? 'a' : 'b';
			}
		}
	}

	WebSocket* pWS = NGMP_OnlineServicesManager::GetWebSocket();;
	pWS->SendData_Signalling(strSignal);
	*/

	// ping?
	int64_t currTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();
	if ((currTime - m_lastPing) > m_timeBetweenUserPings)
	{
		m_lastPing = currTime;
		Ping();
	};

    // Poll the connect worker thread for completion, in place of curl's
    // curl_multi_perform()/curl_multi_info_read().
    if (m_bConnectAttemptDone.load())
    {
        m_bConnectAttemptDone.store(false);

        int httpResponseCode = m_connectHttpStatus;

        /* Check for errors */
        if (!m_bConnectSucceeded)
        {
            m_bConnected = false;
            m_vecWSPartialBuffer.clear();
            NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Failed to connect (WinHTTP error %lu, HTTP status %d)", m_dwConnectWinHttpError, httpResponseCode);

            // reconnecting? give up eventually
            if (m_bReconnecting)
            {
                int maxReconnectAttempts = (TheNGMPGame != nullptr && TheNGMPGame->isGameInProgress()) ? maxReconnectAttempts_Ingame : maxReconnectAttempts_Frontend;

                if (m_numReconnectAttempts >= maxReconnectAttempts || httpResponseCode == 205) // 205 = need full teardown
                {
                    if (httpResponseCode == 205)
                    {
                        NetworkLog(ELogVerbosity::LOG_RELEASE, "Going to teardown (reconnect 205)");
                    }
                    else
                    {
                        NetworkLog(ELogVerbosity::LOG_RELEASE, "Going to teardown (reconnect 2)");
                    }

                    NGMP_OnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::LOST_CONNECTION);
                    m_bConnected = false;
                    m_vecWSPartialBuffer.clear();

                    // clear reconnection flags
                    m_bReconnecting = false;
                    m_numReconnectAttempts = 0;
                    m_lastReconnectAttempt = -1;
                }
            }
            else // give up immediately
            {
                NetworkLog(ELogVerbosity::LOG_RELEASE, "Going to teardown (initial connect)");
                NGMP_OnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::LOST_CONNECTION);
                m_bConnected = false;
                m_vecWSPartialBuffer.clear();

                // clear reconnection flags
                m_bReconnecting = false;
                m_numReconnectAttempts = 0;
                m_lastReconnectAttempt = -1;
            }
        }
        else
        {
            if (m_bReconnecting)
            {
                NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Re-Connected");
            }
            else
            {
                NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Connected");
            }

            /* connected and ready */
            m_bConnected = true;
            m_vecWSPartialBuffer.clear();

            // clear reconnection flags
            m_bReconnecting = false;
            m_numReconnectAttempts = 0;
            m_lastReconnectAttempt = -1;

            // connecting is as good as a pong
            m_lastPong = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();

            // start the long-lived receive thread now that we have a valid websocket handle
            if (m_receiveThread.joinable())
            {
                m_receiveThread.join();
            }
            m_bReceiveThreadShouldStop.store(false);
            m_receiveThread = std::thread(&WebSocket::ReceiveThreadMain, this);

            if (m_fnWebsocketConnectedCallback != nullptr)
            {
                m_fnWebsocketConnectedCallback();
            }
        }
    }

    if (!m_bConnected)
    {
        ReleaseLock();
        return;
    }

	// send anything we have buffered (e.g. things that were queued while not connected)
	for (std::string& strPayload : m_vecQueuedOutboungMsgs)
	{
        DWORD result = WinHttpWebSocketSend(m_hWebSocket, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
            (PVOID)strPayload.data(), (DWORD)strPayload.length());

        if (result != NO_ERROR)
        {
            NetworkLog(ELogVerbosity::LOG_RELEASE, "WinHttpWebSocketSend() failed: %lu\n", result);
        }
	}
	m_vecQueuedOutboungMsgs.clear();

	// do recv -- pop one chunk pushed by ReceiveThreadMain(), in place of a
	// direct curl_ws_recv() call. bIsMessageComplete plays the same role
	// curl's (!CURLWS_CONT && bytesleft == 0) check used to.
	if (!AcquireLock())
	{
		return;
	}
	if (m_vecIncomingMessages.empty())
	{
		ReleaseLock();
		return;
	}
	WSIncomingChunk chunk = std::move(m_vecIncomingMessages.front());
	m_vecIncomingMessages.pop();
	ReleaseLock();

	size_t rlen = chunk.data.size();
	const uint8_t* bufferThisRecv = chunk.data.data();

	if (chunk.bIsExplicitClose)
	{
		// Server sent an explicit WS CLOSE frame -- full teardown, no
		// auto-reconnect (matches the old CURLWS_CLOSE handling).
		NetworkLog(ELogVerbosity::LOG_DEBUG, "Got websocket close");
		NGMP_OnlineServicesManager::GetInstance()->SetPendingFullTeardown(EGOTearDownReason::LOST_CONNECTION);
		m_bConnected = false;
		m_vecWSPartialBuffer.clear();
	}
	else if (chunk.bIsConnectionError)
	{
		// WinHttpWebSocketReceive() itself failed (connection dropped
		// unexpectedly) -- enter the reconnect state machine, matching the
		// old CURLE_RECV_ERROR handling.
		NetworkLog(ELogVerbosity::LOG_RELEASE, "Got websocket disconnect (WinHTTP error %lu), Attempting reconnect", chunk.dwWinHttpError);

		m_bConnected = false;
		m_bReconnecting = true;
		m_numReconnectAttempts = 0;
		m_lastReconnectAttempt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();
		m_vecWSPartialBuffer.clear();

#if defined(GENERALS_ONLINE_USE_SENTRY)
		if (TheNGMPGame != nullptr)
		{
			AsciiString sentryMsg;
			sentryMsg.format("Got websocket disconnect (WinHTTP error %lu), Attempting reconnect", chunk.dwWinHttpError);
			sentry_capture_event(sentry_value_new_message_event(SENTRY_LEVEL_ERROR, "WEBSOCKET_DISCONNECT_ERROR", sentryMsg.str()));
		}
#endif
	}
	else
	{
		NetworkLog(ELogVerbosity::LOG_DEBUG, "Got websocket len: %d", rlen);

		// WinHTTP handles raw WS-protocol PING/PONG control frames
		// internally and never surfaces them here (unlike curl's
		// CURLWS_PING/CURLWS_PONG) -- harmless, since the old handlers for
		// those were empty/TODO no-ops anyway. The real liveness signal is
		// the JSON-level PONG message below, which still arrives as a
		// normal text message.
		if (chunk.bIsText)
		{
			bool bMessageComplete = false;

			static constexpr size_t MAX_WS_PARTIAL_SIZE = 2 * 1024 * 1024; // 2 MB
			if (m_vecWSPartialBuffer.size() + rlen > MAX_WS_PARTIAL_SIZE)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[WebSocket] Partial buffer overflow, discarding message");
					m_vecWSPartialBuffer.clear();
					return;
				}
				
				// SECURITY FIX: Store old size BEFORE resize to avoid off-by-one error in memcpy
				size_t oldSize = m_vecWSPartialBuffer.size();
				m_vecWSPartialBuffer.resize(oldSize + rlen);
				memcpy_s(m_vecWSPartialBuffer.data() + oldSize, rlen, bufferThisRecv, rlen);

				// WinHttpWebSocketReceive() tells us directly (via the buffer
				// type it returns) whether this was a fragment or the final
				// piece of the message -- no offset/bytesleft bookkeeping
				// needed the way curl's meta struct required.
				bMessageComplete = chunk.bIsMessageComplete;
				NetworkLog(ELogVerbosity::LOG_DEBUG, "WEBSOCKET CHUNK OF SIZE %d! [MESSAGE COMPLETE: %d]", rlen, bMessageComplete);

				if (bMessageComplete)
				{
					try
					{
						// null terminate buffer
						m_vecWSPartialBuffer.push_back('\0');

						// process it
						nlohmann::json jsonObject;
						bool bDeserializedOK = JSONDeserialize(m_vecWSPartialBuffer.data(), &jsonObject);

						// clear buffer and resize
						m_vecWSPartialBuffer.clear();
						m_vecWSPartialBuffer.resize(0);

						if (bDeserializedOK)
						{
							if (jsonObject.contains("msg_id"))
							{
								WebSocketMessageBase msgDetails;
								bool bParsedBase = JSONGetAsObject<WebSocketMessageBase>(jsonObject, &msgDetails);

								if (bParsedBase)
								{
									EWebSocketMessageID msgID = msgDetails.msg_id;

									switch (msgID)
									{
									case EWebSocketMessageID::MODERATION_NOTICE:
									{
										WebSocketMessage_ModerationNotice moderationNotice;
										if (JSONGetAsObject(jsonObject, &moderationNotice))
										{
											HandleModerationNotice(
												moderationNotice.action_type,
												moderationNotice.reason,
												moderationNotice.scope_type);
										}
									}
									break;

									case EWebSocketMessageID::MODERATION_COMMAND_RESULT:
									{
										WebSocketMessage_ModerationCommandResult commandResult;
										if (JSONGetAsObject(jsonObject, &commandResult))
										{
											NetworkLog(
												ELogVerbosity::LOG_RELEASE,
												"Moderation command %llu %s: %s",
												static_cast<unsigned long long>(commandResult.request_id),
												commandResult.success ? "succeeded" : "failed",
												commandResult.message.c_str());
										}
									}
									break;

									case EWebSocketMessageID::PONG:
									{
										int64_t currTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();
										m_lastPong = currTime;
									}
									break;

									case EWebSocketMessageID::NETWORK_ROOM_CHAT_FROM_SERVER:
									{
										WebSocketMessage_RoomChatIncoming chatData;
										bool bParsed = JSONGetAsObject(jsonObject, &chatData);

										if (bParsed)
										{
											SYSTEMTIME systemTime;
											GetLocalTime(&systemTime);

											UnicodeString unicodeStr;
											unicodeStr.format(L"[%2.2d:%2.2d] %s", systemTime.wHour, systemTime.wMinute, from_utf8(chatData.message).c_str());

											Color color = DetermineColorForChatMessage(EChatMessageType::CHAT_MESSAGE_TYPE_NETWORK_ROOM, true, chatData.action, chatData.admin, chatData.name_change);

											NGMP_OnlineServices_RoomsInterface* pRoomsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
											if (pRoomsInterface != nullptr && pRoomsInterface->m_OnChatCallback != nullptr)
											{
												pRoomsInterface->m_OnChatCallback(unicodeStr, color);
											}
										}
									}
									break;

									case EWebSocketMessageID::SOCIAL_FRIEND_CHAT_MESSAGE_SERVER_TO_CLIENT:
									{
										WebSocketMessage_Social_FriendChatMessage_Incoming chatData;
										bool bParsed = JSONGetAsObject(jsonObject, &chatData);

										if (bParsed)
										{
											UnicodeString unicodeStr(from_utf8(chatData.message).c_str());

											NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
											if (pSocialInterface != nullptr)
											{
												pSocialInterface->OnChatMessage(chatData.source_user_id, chatData.target_user_id, unicodeStr);
											}
										}
									}
									break;

									case EWebSocketMessageID::SOCIAL_FRIEND_ONLINE_STATUS_CHANGED:
									{
										WebSocketMessage_Social_FriendStatusChanged statusChangedData;
										bool bParsed = JSONGetAsObject(jsonObject, &statusChangedData);

										if (bParsed)
										{
											NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
											if (pSocialInterface != nullptr)
											{
												pSocialInterface->OnOnlineStatusChanged(statusChangedData.display_name, statusChangedData.online);
											}
										}
									}
									break;

									case EWebSocketMessageID::SOCIAL_FRIEND_FRIEND_REQUEST_ACCEPTED_BY_TARGET:
									{
										WebSocketMessage_Social_FriendRequestAccepted statusChangedData;
										bool bParsed = JSONGetAsObject(jsonObject, &statusChangedData);

										if (bParsed)
										{
											NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
											if (pSocialInterface != nullptr)
											{
												pSocialInterface->OnFriendRequestAccepted(statusChangedData.display_name);
											}
										}
									}
									break;

									case EWebSocketMessageID::SOCIAL_FRIENDS_LIST_DIRTY:
									{
                                        // nothing to parse here, it's just an event only
                                        extern void updateBuddyInfo(bool bIsAutoRefresh = false, bool bUseCache = false);
										updateBuddyInfo(true);
									}
									break;

									case EWebSocketMessageID::SOCIAL_CANT_ADD_FRIEND_LIST_FULL:
									{
										// always show this notification, it's tied to a local user action
										showNotificationBox(AsciiString::TheEmptyString, UnicodeString(L"Cannot sent friends request. Your friends list is full."));
									}
									break;

									case EWebSocketMessageID::SOCIAL_FRIENDS_OVERALL_STATUS_UPDATE:
									{
										WebSocketMessage_FriendsOverallStatusUpdate statusUpdateData;
										bool bParsed = JSONGetAsObject(jsonObject, &statusUpdateData);

										if (bParsed)
										{
											UnicodeString strFormat = UnicodeString::TheEmptyString;
											if (statusUpdateData.num_online > 0 && statusUpdateData.num_pending > 0)
											{
												strFormat.format(L"You have %d friend(s) online and %d pending friend request(s)", statusUpdateData.num_online, statusUpdateData.num_pending);
											}
											else if (statusUpdateData.num_online > 0)
											{
												strFormat.format(L"You have %d friend(s) online.", statusUpdateData.num_online);
											}
											else if (statusUpdateData.num_pending > 0)
											{
												strFormat.format(L"You have %d pending friend request(s)", statusUpdateData.num_pending);
											}
											else
											{
												strFormat = UnicodeString(L"Press F5 or INSERT to bring up the communicator at any time (including in-game).");
											}

											// show it on the communicator too
											if (statusUpdateData.num_pending > 0)
											{
                                                NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
                                                if (pSocialInterface != nullptr)
                                                {
													pSocialInterface->RegisterInitialPendingRequestsUponLogin(statusUpdateData.num_pending);
                                                }
											}

											if (!strFormat.isEmpty())
											{
												// always show this notification
												showNotificationBox(AsciiString::TheEmptyString, strFormat);
											}
										}
									}
									break;

									case EWebSocketMessageID::START_GAME:
									{
                                        WebSocketMessage_StartGameResponse startGameData;
                                        bool bParsed = JSONGetAsObject(jsonObject, &startGameData);

										if (bParsed)
										{
											// store URL
                                            NGMP_OnlineServicesManager::GetInstance()->SetScreenshotS3URI_StartMatch(startGameData.screenshot_url.c_str());
										}

										// always start, even if we couldnt parse the url
                                        NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
                                        if (pLobbyInterface != nullptr && pLobbyInterface->m_callbackStartGamePacket != nullptr)
                                        {
                                            pLobbyInterface->m_callbackStartGamePacket();
                                        }
									}
									break;

									case EWebSocketMessageID::FULL_MESH_CONNECTIVITY_CHECK_RESPONSE:
									{
										int64_t meshCheckID = 0;
										int meshCheckAttempt = 0;
										if (jsonObject.contains("mesh_check_id") && jsonObject["mesh_check_id"].is_number_integer())
										{
											meshCheckID = jsonObject["mesh_check_id"].get<int64_t>();
										}
										if (jsonObject.contains("attempt") && jsonObject["attempt"].is_number_integer())
										{
											meshCheckAttempt = jsonObject["attempt"].get<int>();
										}

										// respond with our state
										std::vector<int64_t> connectivityMap;
										NetworkMesh* pMesh = nullptr;
										NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
										if (pLobbyInterface != nullptr)
										{
											pMesh = pLobbyInterface->GetNetworkMeshForLobby();
										}

										if (pMesh != nullptr)
										{
											for (auto& conn : pMesh->GetAllConnections())
											{
												int64_t userID = conn.first;
												PlayerConnection& playerConn = conn.second;

												if (playerConn.GetState() == EConnectionState::CONNECTED_DIRECT)
												{
													// NOTE: Useful for testing
													//if (userID != 1)
													{
														connectivityMap.push_back(userID);
													}
												}
											}
										}

										// send response
										nlohmann::json j;
										j["msg_id"] = EWebSocketMessageID::FULL_MESH_CONNECTIVITY_CHECK_RESPONSE;
										j["mesh_check_id"] = meshCheckID;
										j["attempt"] = meshCheckAttempt;
										j["connectivity_map"] = connectivityMap;
										std::string strBody = j.dump();

										Send(strBody.c_str());
										break;
									}

									case EWebSocketMessageID::FULL_MESH_CONNECTIVITY_CHECK_RESPONSE_COMPLETE_TO_HOST:
									{
										// all checks are done, process start for host

										bool bMeshComplete = false;

										try
										{
											jsonObject["mesh_complete"].get_to(bMeshComplete);

											std::list<std::pair<int64_t, int64_t>> missingConnections;
											if (!bMeshComplete)
											{
												NetworkLog(ELogVerbosity::LOG_RELEASE, "[FULL_MESH_CONNECTIVITY_CHECK_RESPONSE_COMPLETE_TO_HOST] Mesh is not complete for someone");
												for (const auto& missingConnectionEntryIter : jsonObject["missing_connections"])
												{
													int64_t source_user_id = -1;
													int64_t target_user_id = -1;

													missingConnectionEntryIter["source_user_id"].get_to(source_user_id);
													missingConnectionEntryIter["target_user_id"].get_to(target_user_id);

													missingConnections.push_back(std::make_pair(source_user_id, target_user_id));
												}
											}
											else
											{
												NetworkLog(ELogVerbosity::LOG_RELEASE, "[FULL_MESH_CONNECTIVITY_CHECK_RESPONSE_COMPLETE_TO_HOST] Mesh is fully complete");
											}

											// invoke callback
											if (m_cbOnConnectivityCheckComplete != nullptr)
											{
												m_cbOnConnectivityCheckComplete(bMeshComplete, missingConnections);
											}

											m_cbOnConnectivityCheckComplete = NULL;
										}
										catch (...)
										{
											NetworkLog(ELogVerbosity::LOG_RELEASE, "[FULL_MESH_CONNECTIVITY_CHECK_RESPONSE_COMPLETE_TO_HOST] Error processing response");
											break;
										}

										break;
									}

									case EWebSocketMessageID::NETWORK_CONNECTION_START_SIGNALLING:
									{
										WebSocketMessage_NetworkStartSignalling startSignallingData;
										bool bParsed = JSONGetAsObject(jsonObject, &startSignallingData);

										// TODO_NGMP: Better location for this
										// When we find a new player, get their latest stats. Tooltip and loading screen need it, so we'll grab it now and then use cached data later since it cannot possibly change while in a lobby
										NGMP_OnlineServices_StatsInterface* pStatsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_StatsInterface>();
										if (pStatsInterface != nullptr)
										{
											pStatsInterface->findPlayerStatsByID(startSignallingData.user_id, [=](bool bSuccess, PSPlayerStats stats)
												{

												}, EStatsRequestPolicy::BYPASS_CACHE_FORCE_REQUEST);
										}

										if (bParsed)
										{
											NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
											if (pLobbyInterface != nullptr)
											{
												NetworkMesh* pMesh = pLobbyInterface->GetNetworkMeshForLobby();

												if (pMesh != nullptr)
												{
                                                    pMesh->StartConnectionSignalling(startSignallingData.middleware_id.c_str(), startSignallingData.user_id, startSignallingData.preferred_port);
                                                    NetworkLog(ELogVerbosity::LOG_RELEASE, "[NETWORK_CONNECTION_START_SIGNALLING] Starting signalling with %lld (MWID: %s)", startSignallingData.user_id, startSignallingData.middleware_id.c_str());
												}
												else
												{
													NetworkLog(ELogVerbosity::LOG_RELEASE, "[NETWORK_CONNECTION_START_SIGNALLING] Network mesh is null");
													break;
												}
											}
											else
											{
												NetworkLog(ELogVerbosity::LOG_RELEASE, "[NETWORK_CONNECTION_START_SIGNALLING] Lobby interface is null");
												break;
											}
										}
									}
									break;

									case EWebSocketMessageID::AC_REGISTER_PLAYER:
                                    {
                                        WebSocketMessage_ACRegisterPlayer acData;
                                        bool bParsed = JSONGetAsObject(jsonObject, &acData);

										if (bParsed)
										{
											NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Websocket AC_REGISTER_PLAYER for %lld and %s", acData.user_id, acData.mwid);
											if (!AnticheatPlugInterface::RegisterPlayer(acData.mwid, acData.user_id))
											{
												NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] AnticheatPlugInterface::RegisterPlayer failed");
											}
										}
                                    }
                                    break;

                                    case EWebSocketMessageID::AC_DEREGISTER_PLAYER:
                                    {
                                        WebSocketMessage_ACDeregisterPlayer acData;
                                        bool bParsed = JSONGetAsObject(jsonObject, &acData);

										if (bParsed)
										{
											NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] Websocket AC_DEREGISTER_PLAYER for %lld and %s", acData.user_id, acData.mwid);
											if (!AnticheatPlugInterface::DeregisterPlayer(acData.mwid, acData.user_id))
											{
												NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] AnticheatPlugInterface::DeregisterPlayer failed");
											}
										}
                                    }
                                    break;

									case EWebSocketMessageID::NETWORK_CONNECTION_DISCONNECT_PLAYER:
									{
										WebSocketMessage_NetworkDisconnectPlayer disconnectPlayerData;
										bool bParsed = JSONGetAsObject(jsonObject, &disconnectPlayerData);

										if (bParsed)
										{
											NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
											if (pLobbyInterface != nullptr)
											{
												int64_t currentLobbyID = pLobbyInterface->GetCurrentLobby().lobbyID;

												if (currentLobbyID == -1 || currentLobbyID != disconnectPlayerData.lobby_id)
												{
													NetworkLog(ELogVerbosity::LOG_RELEASE, "[NETWORK_CONNECTION_DISCONNECT_PLAYER] Lobby ID mismatch! Expected %lld, got %lld", currentLobbyID, disconnectPlayerData.lobby_id);
													break;
												}

												NetworkMesh* pMesh = pLobbyInterface->GetNetworkMeshForLobby();

												if (pMesh != nullptr)
												{
													pMesh->DisconnectUser(disconnectPlayerData.user_id);
												}
												else
												{
													NetworkLog(ELogVerbosity::LOG_RELEASE, "[NETWORK_CONNECTION_DISCONNECT_PLAYER] Network mesh is null");
													break;
												}
											}
											else
											{
												NetworkLog(ELogVerbosity::LOG_RELEASE, "[NETWORK_CONNECTION_DISCONNECT_PLAYER] Lobby interface is null");
												break;
											}
										}
									}
									break;

									case EWebSocketMessageID::NETWORK_SIGNAL:
									{
										NetworkLog(ELogVerbosity::LOG_RELEASE, "[SIGNAL] GOT SIGNAL!");

										WebSocketMessage_NetworkSignal signalData;
										bool bParsed = JSONGetAsObject(jsonObject, &signalData);

										if (bParsed)
										{
											NetworkLog(ELogVerbosity::LOG_RELEASE, "[SIGNAL] Signal User: %lld!", signalData.target_user_id);
											NetworkLog(ELogVerbosity::LOG_RELEASE, "[SIGNAL] Signal Payload Size: %d!", (int)signalData.payload.size());
											m_pendingSignals.push(signalData.payload);
										}
									}
									break;

									case EWebSocketMessageID::LOBBY_CHAT_FROM_SERVER:
									{
										WebSocketMessage_LobbyChatIncoming chatData;
										bool bParsed = JSONGetAsObject(jsonObject, &chatData);

										if (bParsed)
										{
											UnicodeString unicodeStr(from_utf8(chatData.message).c_str());

											NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
											if (pLobbyInterface != nullptr)
											{
												int lobbySlot = -1;
												auto lobbyMembers = pLobbyInterface->GetMembersListForCurrentRoom();
												for (const auto& lobbyMember : lobbyMembers)
												{
													if (lobbyMember.user_id == chatData.user_id)
													{
														lobbySlot = lobbyMember.m_SlotIndex;
														break;
													}
												}

												// Match local setup notice colors.
												// System announcements stay neutral even when they come from the host;
												// player chat keeps its per-slot color.
												Color color = chatData.announcement
													? DetermineSystemNoticeColor(false, false)
													: DetermineColorForChatMessage(EChatMessageType::CHAT_MESSAGE_TYPE_LOBBY, true, chatData.action, false, false, lobbySlot);

												if (pLobbyInterface->m_OnChatCallback != nullptr)
												{
													pLobbyInterface->m_OnChatCallback(unicodeStr, color);
												}
											}
										}
									}
									break;

									case EWebSocketMessageID::NETWORK_ROOM_MEMBER_LIST_UPDATE:
									{
										std::unordered_map<uint64_t, NetworkRoomMember> mapMembers;
										for (const auto& playerEntryIter : jsonObject["members"])
										{
											NetworkRoomMember newMember;
											playerEntryIter["UserID"].get_to(newMember.user_id);
											playerEntryIter["Name"].get_to(newMember.display_name);
											playerEntryIter["IsAdmin"].get_to(newMember.m_bIsAdmin);

											mapMembers.emplace(newMember.user_id, newMember);
										}

										RoomSelectionResult selectionResult;
										if (jsonObject.contains("selected_room_id") && jsonObject["selected_room_id"].is_number_integer())
										{
											selectionResult.selectedRoomID = jsonObject["selected_room_id"].get<int>();
										}
										if (jsonObject.contains("effective_room_id") && jsonObject["effective_room_id"].is_number_integer())
										{
											selectionResult.effectiveRoomID = jsonObject["effective_room_id"].get<int>();
										}
										if (jsonObject.contains("rejected_room_id") && jsonObject["rejected_room_id"].is_number_integer())
										{
											selectionResult.rejectedRoomID = jsonObject["rejected_room_id"].get<int>();
										}
										if (jsonObject.contains("room_selection_error") && jsonObject["room_selection_error"].is_string())
										{
											jsonObject["room_selection_error"].get_to(selectionResult.error);
										}
										if (jsonObject.contains("request_id") && jsonObject["request_id"].is_number_unsigned())
										{
											selectionResult.requestID = jsonObject["request_id"].get<uint64_t>();
										}

                                        NGMP_OnlineServices_RoomsInterface* pRoomsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
                                        if (pRoomsInterface != nullptr)
                                        {
											pRoomsInterface->OnRosterUpdated(std::move(mapMembers), selectionResult);
                                        }
									}
									break;

                                    case EWebSocketMessageID::ANTICHEAT_MESSAGE:
                                    {
                                        NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] GOT AC MSG FROM WEBSOCKET!");

										WebSocketMessage_AnticheatMessage acMsg;
                                        bool bParsed = JSONGetAsObject(jsonObject, &acMsg);

                                        if (bParsed)
                                        {
                                            NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] AC Msg Signal User: %lld!", acMsg.target_user_id);
                                            NetworkLog(ELogVerbosity::LOG_RELEASE, "[AC] AC Msg Signal Payload Size: %d!", (int)acMsg.payload.size());
                                            AnticheatPlugInterface::AC_NetworkMessageArrived(acMsg.target_user_id, acMsg.payload.data(), acMsg.payload.size());
                                        }
                                    }
                                    break;

									case EWebSocketMessageID::LOBBY_CURRENT_LOBBY_UPDATE:
									{
										// re-get the room info as it is stale
										NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
										if (pLobbyInterface != nullptr)
										{
											pLobbyInterface->UpdateRoomDataCache(nullptr);
										}
									}
									break;

									case EWebSocketMessageID::PROBE:
									{
										WebSocketMessage_ServerProbe probe;
                                        bool bParsed = JSONGetAsObject(jsonObject, &probe);

										if (bParsed)
										{
											NetworkLog(ELogVerbosity::LOG_RELEASE, "[PROBE] GOT PROBE REQUEST: %s!", probe.url.c_str());

											NGMP_OnlineServicesManager::GetInstance()->CaptureScreenshotForProbe(EScreenshotType::SCREENSHOT_TYPE_GAMEPLAY, probe.url);

											// service needs the response
											nlohmann::json j;
											j["msg_id"] = EWebSocketMessageID::PROBE_RESP;
											j["timestamp"] = "0";
											std::string strBody = j.dump();
											Send(strBody.c_str());
										}
									}
									break;

									case EWebSocketMessageID::NETWORK_ROOM_LOBBY_LIST_UPDATE:
									{
										// re-get the room info as it is stale
										NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
										if (pLobbyInterface != nullptr)
										{
											pLobbyInterface->SetLobbyListDirty();
										}
									}
									break;

									case EWebSocketMessageID::MATCHMAKING_ACTION_JOIN_PREARRANGED_LOBBY:
									{
										WebSocketMessage_MatchmakingAction_JoinPrearrangedLobby mmEvent;
										bool bParsed = JSONGetAsObject(jsonObject, &mmEvent);

										if (bParsed)
										{
											NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
											if (pLobbyInterface != nullptr)
											{
												pLobbyInterface->InvokeMatchmakingMatchFoundCallback();

												// TODO_QUICKMATCH: Only if really in quickmatch

												// TODO_QUICKMATCH: We need to retrieve this info instead
												// basic info needed to join
												LobbyEntry lobbyEntry;
												lobbyEntry.lobbyID = mmEvent.lobby_id;
												lobbyEntry.map_path = "Maps\\Alpine Assault\\Alpine Assault.map";

												pLobbyInterface->JoinLobby(lobbyEntry, std::string());

												pLobbyInterface->InvokeMatchmakingMessageCallback("Joining QuickMatch Lobby");
											}
											else
											{
												NetworkLog(ELogVerbosity::LOG_RELEASE, "[NETWORK_CONNECTION_DISCONNECT_PLAYER] Lobby interface is null");
												break;
											}
										}
									}
									break;

									case EWebSocketMessageID::MATCHMAKING_ACTION_START_GAME:
									{
										NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
										if (pLobbyInterface != nullptr)
										{
											pLobbyInterface->InvokeMatchmakingStartGameCallback();
										}
									}
									break;

									case EWebSocketMessageID::MATCHMAKING_ACTION_REQUEUE:
									{
										NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
										if (pLobbyInterface != nullptr)
										{
											pLobbyInterface->ResetForMatchmakingRequeue();
											pLobbyInterface->InvokeMatchmakingRequeueCallback();
										}
									}
									break;

									case EWebSocketMessageID::MATCHMAKING_ACTION_SETUP_PROGRESS:
									{
										int timeoutMs = 0;
										if (jsonObject.contains("timeout_ms") && jsonObject["timeout_ms"].is_number_integer())
										{
											timeoutMs = jsonObject["timeout_ms"].get<int>();
										}

										NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
										if (pLobbyInterface != nullptr && timeoutMs > 0)
										{
											pLobbyInterface->InvokeMatchmakingSetupProgressCallback(timeoutMs);
										}
									}
									break;

									case EWebSocketMessageID::MATCHMAKING_MESSAGE:
									{
										WebSocketMessage_MatchmakingMessage matchmakingMsg;
										bool bParsed = JSONGetAsObject(jsonObject, &matchmakingMsg);

										if (bParsed)
										{
											NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
											if (pLobbyInterface != nullptr)
											{
												pLobbyInterface->InvokeMatchmakingMessageCallback(matchmakingMsg.message);
											}
										}
									}
									break;

									case EWebSocketMessageID::SOCIAL_NEW_FRIEND_REQUEST:
									{
										WebSocketMessage_Social_NewFriendRequest incomingNotify;
										bool bParsed = JSONGetAsObject(jsonObject, &incomingNotify);

										if (bParsed)
										{
											NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
											if (pSocialInterface != nullptr)
											{
												pSocialInterface->InvokeCallback_NewFriendRequest(incomingNotify.display_name);
											}
										}
									}
									break;

									case EWebSocketMessageID::WS_KEEPALIVE:
									{
										std::vector<GOModuleInfo> modules = GetLoadedModules();

										std::vector<std::vector<std::string>> vecResp;

                                        for (auto& m : modules)
										{
											std::vector<std::string> newEntry(2);
											newEntry[0] = m.path;
											newEntry[1] = std::to_string(m.size);
											vecResp.push_back(newEntry);
                                        }

                                        // service needs the response
                                        nlohmann::json j;
                                        j["msg_id"] = EWebSocketMessageID::WS_KEEPALIVE_CLIENT;
                                        j["resp"] = vecResp;
                                        std::string strBody = j.dump();
                                        Send(strBody.c_str());
									}
									break;

									default:
										NetworkLog(ELogVerbosity::LOG_RELEASE, "Unhandled WebSocketMessage: %d", (int)msgID);
										break;
									}
								}
								else
								{
									NetworkLog(ELogVerbosity::LOG_RELEASE, "Malformed WebSocketMessage: couldn't parse as WebSocketMessageBase");
								}
							}
						}
						else
						{
							NetworkLog(ELogVerbosity::LOG_RELEASE, "Malformed WebSocketMessage");
						}
					}
					catch (nlohmann::json::exception& jsonException)
					{

						NetworkLog(ELogVerbosity::LOG_RELEASE, "Unparsable WebSocketMessage 101: %s (JSON: %s)", bufferThisRecv, jsonException.what());
						NetworkLog(ELogVerbosity::LOG_RELEASE, "Buildup buffer is: %s", m_vecWSPartialBuffer.data());

						m_vecWSPartialBuffer.clear();
					}
					catch (std::exception& e)
					{
						NetworkLog(ELogVerbosity::LOG_RELEASE, "Unparsable WebSocketMessage 100: %s (%s)", bufferThisRecv, e.what());

						m_vecWSPartialBuffer.clear();
					}
					catch (...)
					{
						NetworkLog(ELogVerbosity::LOG_RELEASE, "Unparsable WebSocketMessage 102: %s", bufferThisRecv);

						m_vecWSPartialBuffer.clear();
					}
				}
			}
			else
			{
				NetworkLog(ELogVerbosity::LOG_DEBUG, "Got websocket binary");
				// noop -- matches the old CURLWS_BINARY handling
			}
		}

	// time since last pong?
	if (m_lastPong != -1 && (currTime - m_lastPong) >= m_timeForWSTimeout)
	{
        // send event to sentry
#if defined(GENERALS_ONLINE_USE_SENTRY)
        if (TheNGMPGame != nullptr)
        {
            AsciiString sentryMsg;
            sentryMsg.format("Got websocket disconnect (Timeout), timeout is %lld, last pong was at %lld, current time is %lld, attempting reconnect", currTime - m_lastPong, m_lastPong, currTime);
            sentry_capture_event(sentry_value_new_message_event(SENTRY_LEVEL_ERROR, "WEBSOCKET_DISCONNECT_TIMEOUT", sentryMsg.str()));
        }
#endif

		NetworkLog(ELogVerbosity::LOG_RELEASE, "Got websocket disconnect (Timeout), timeout is %lld, last pong was at %lld, current time is %lld, attempting reconnect", currTime - m_lastPong, m_lastPong, currTime);
        m_bConnected = false;
        m_bReconnecting = true;
        m_numReconnectAttempts = 0;
        m_lastReconnectAttempt = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();
        m_vecWSPartialBuffer.clear();
	};

	ReleaseLock();
}

NGMP_OnlineServices_RoomsInterface::NGMP_OnlineServices_RoomsInterface()
{

}

void NGMP_OnlineServices_RoomsInterface::GetRoomList(std::function<void(bool)> cb)
{
	m_vecRooms.clear();
	m_CurrentRoomIndex = -1;
	m_EffectiveRoomID.reset();
	m_PendingRoomChange.reset();
	m_bRoomSelectionResultsSupported = false;
	m_bSupportsModerationCommands = false;

	// Cache our buddies on lobby list
	NGMP_OnlineServices_SocialInterface* pSocialInterface =
		NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if (pSocialInterface != nullptr)
	{
		pSocialInterface->GetFriendsList(false, nullptr);
	}

	std::string strURI = NGMP_OnlineServicesManager::GetAPIEndpoint("Rooms");
	std::map<std::string, std::string> mapHeaders;

	NGMP_OnlineServicesManager::GetInstance()->GetHTTPManager()->SendGETRequest(strURI.c_str(), EIPProtocolVersion::DONT_CARE, mapHeaders, [=](bool bSuccess, int statusCode, std::string strBody, HTTPRequest* pReq)
		{
			if (!bSuccess || statusCode != 200)
			{
				cb(false);
				return;
			}

			try
			{
				std::vector<NetworkRoom> rooms;
				nlohmann::json jsonObject = nlohmann::json::parse(strBody);
				const bool roomSelectionResultsSupported = jsonObject.value("supports_room_selection_results", false);
				const bool supportsModerationCommands = jsonObject.value("supports_moderation_commands", false);

				for (const auto& roomEntryIter : jsonObject["rooms"])
				{
					int id = 0;
					std::string strName;
					ERoomFlags flags = ERoomFlags::ROOM_FLAGS_DEFAULT;
					int parentRoomID = -1;

					roomEntryIter["id"].get_to(id);
					roomEntryIter["name"].get_to(strName);
					if (roomEntryIter.contains("flags") && !roomEntryIter["flags"].is_null())
					{
						roomEntryIter["flags"].get_to(flags);
					}
					if (roomEntryIter.contains("parent_id") && !roomEntryIter["parent_id"].is_null())
					{
						roomEntryIter["parent_id"].get_to(parentRoomID);
					}
					rooms.emplace_back(id, strName, flags, parentRoomID);
				}

				m_vecRooms = std::move(rooms);
				m_bRoomSelectionResultsSupported = roomSelectionResultsSupported;
				m_bSupportsModerationCommands = supportsModerationCommands;

				cb(true);
				return;
			}
			catch (const std::exception& exception)
			{
				NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Failed to parse room list: %s", exception.what());
			}

			cb(false);
			return;
		});
}

void NGMP_OnlineServices_RoomsInterface::JoinRoom(int roomIndex)
{
	const std::vector<NetworkRoom>& rooms = GetGroupRooms();
	if (roomIndex < 0 || roomIndex >= (int)rooms.size())
	{
		ReportRoomJoinFailure(std::format("Invalid room index {}.", roomIndex));
		return;
	}

	std::shared_ptr<WebSocket> pWS = NGMP_OnlineServicesManager::GetWebSocket();
	if (pWS == nullptr)
	{
		ReportRoomJoinFailure("The room service is not connected.");
		return;
	}

	if (m_PendingRoomChange.has_value())
	{
		ReportRoomJoinFailure("Another room change is already in progress.");
		return;
	}

	const uint64_t requestID = m_NextRoomChangeRequestID++;
	m_PendingRoomChange = PendingRoomChange{
		roomIndex,
		rooms[roomIndex].GetRoomID(),
		requestID,
		std::chrono::steady_clock::now() + std::chrono::seconds(10)
	};
	pWS->SendData_JoinNetworkRoom(rooms[roomIndex].GetRoomID(), requestID);
}

std::unordered_map<uint64_t, NetworkRoomMember>& NGMP_OnlineServices_RoomsInterface::GetMembersListForCurrentRoom()
{
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Repopulating network room roster using local data");
	return m_mapMembers;
}

void NGMP_OnlineServices_RoomsInterface::SendChatMessageToCurrentRoom(UnicodeString& strChatMsgUnicode, bool bIsAction)
{
	std::shared_ptr<WebSocket>  pWS = NGMP_OnlineServicesManager::GetWebSocket();;
	if (pWS != nullptr)
	{
		pWS->SendData_RoomChatMessage(strChatMsgUnicode, bIsAction);
	}
}

void NGMP_OnlineServices_RoomsInterface::OnRosterUpdated(std::unordered_map<uint64_t, NetworkRoomMember> mapMembers,
	const RoomSelectionResult& selectionResult)
{
	m_mapMembers = std::move(mapMembers);

	int changedRoomIndex = -1;
	bool effectiveRoomChanged = true;
	bool refreshRoster = true;
	std::string roomJoinFailure;
	if (m_PendingRoomChange.has_value())
	{
		const PendingRoomChange& pendingRoomChange = *m_PendingRoomChange;
		const bool requestMatches = !selectionResult.requestID.has_value()
			|| pendingRoomChange.requestID == *selectionResult.requestID;
		if (requestMatches && selectionResult.rejectedRoomID == pendingRoomChange.roomID)
		{
			roomJoinFailure = selectionResult.error.empty() ? "The room selection was rejected." : selectionResult.error;
			m_PendingRoomChange.reset();
		}
		else
		{
			const bool selectionMatches = requestMatches
				&& (selectionResult.selectedRoomID.has_value()
					? pendingRoomChange.roomID == *selectionResult.selectedRoomID
					: !m_bRoomSelectionResultsSupported);
			if (selectionMatches)
			{
				if (selectionResult.effectiveRoomID.has_value())
				{
					effectiveRoomChanged = !m_EffectiveRoomID.has_value()
						|| *m_EffectiveRoomID != *selectionResult.effectiveRoomID;
					m_EffectiveRoomID = selectionResult.effectiveRoomID;
					refreshRoster = effectiveRoomChanged;
				}
				m_CurrentRoomIndex = pendingRoomChange.roomIndex;
				changedRoomIndex = m_CurrentRoomIndex;
				m_PendingRoomChange.reset();
			}
		}
	}

	if (changedRoomIndex >= 0 && m_RoomChangedCallback != nullptr)
	{
		m_RoomChangedCallback(changedRoomIndex, effectiveRoomChanged);
	}
	if (!roomJoinFailure.empty())
	{
		ReportRoomJoinFailure(roomJoinFailure);
	}

	std::scoped_lock<std::mutex> lock(m_rosterCallbackMutex);
	if (refreshRoster && m_RosterNeedsRefreshCallback != nullptr)
	{
		m_RosterNeedsRefreshCallback();
	}
}

void NGMP_OnlineServices_RoomsInterface::Tick()
{
	if (m_PendingRoomChange.has_value()
		&& std::chrono::steady_clock::now() >= m_PendingRoomChange->deadline)
	{
		m_PendingRoomChange.reset();
		ReportRoomJoinFailure("The room change timed out. Please try again.");
	}
}

void NGMP_OnlineServices_RoomsInterface::ReportRoomJoinFailure(const std::string& error)
{
	NetworkLog(ELogVerbosity::LOG_RELEASE, "[NGMP] Room change failed: %s", error.c_str());
	if (m_OnChatCallback != nullptr)
	{
		UnicodeString message;
		message = L"Couldn't join that room. Please try again.";
		m_OnChatCallback(message, GameMakeColor(255, 0, 0, 255));
	}
}



